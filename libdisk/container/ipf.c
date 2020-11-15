/*
 * libdisk/container_ipf.c
 * 
 * Write-only SPS/CAPS IPF support.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* crc32("User IPF") -- Arbitrary ID for identifying generated IPFs.
 * This is stamped into the INFO release, revision, and userid fields.
 * ** IMPORTANT **
 * Please respect the SPS and do not change these field values. They are of no 
 * interest to the IPF decoder library, but allow our IPFs to be easily
 * distinguished from preserved images from the SPS library. */
#define IPF_ID 0x843265bbu

/* Encoder types:
 *  * ENC_SPS is the newer more flexible encoding format, capable of
 *    representing arbitrary-size and -alignment bitstreams.
 *  * ENC_CAPS is more widely supported but requires data to be byte-aligned.
 *    Supported by v2 of the IPF decoder library (unlike ENC_SPS). */
#define ENC_CAPS 1 /* legacy */
#define ENC_SPS  2 /* capable of bit-oriented data streams */

/* Number of MFM cells to pre-pend to first block of each track. We do this to 
 * avoid the write splice interfering with real track data, when writing an 
 * IPF image to disk with Kryoflux.
 *  - Must be a multiple of 2, since we are encoding MFM data *and* clock.
 *  - Must be a multiple of 16 to keep stream byte-aligned for CAPS encoding.
 * NB. Recent versions of the Kryoflux DTC tool do not have this problem, and
 * we can set PREPEND_BITS to 0. */
/*#define PREPEND_BITS 32*/
#define PREPEND_BITS 0

/* Maximum bounds for track data. */
#define MAX_BLOCKS_PER_TRACK 100
#define MAX_DATA_PER_TRACK   (MAX_BLOCKS_PER_TRACK * 1024)

struct ipf_header {
    uint8_t id[4];
    uint32_t len;
    uint32_t crc;
};

struct ipf_info {
    uint32_t type;
    uint32_t encoder, encrev;
    uint32_t release, revision;
    uint32_t origin;
    uint32_t mincyl, maxcyl;
    uint32_t minhead, maxhead;
    uint32_t date, time;
    uint32_t platform[4];
    uint32_t disknum;
    uint32_t userid;
    uint32_t reserved[3];
};

struct ipf_img {
    uint32_t cyl, head;
    uint32_t dentype;  /* enum dentype */
    uint32_t sigtype;  /* 1 */
    uint32_t trksize;  /* ceil(trkbits/8) */
    uint32_t startpos; /* floor(startbit/8) */
    uint32_t startbit; /* bit offset from index of data start */
    uint32_t databits; /* # raw MFM cells */
    uint32_t gapbits;  /* # raw MFM cells */
    uint32_t trkbits;  /* databits + gapbits */
    uint32_t blkcnt;   /* e.g., 11 for DOS */
    uint32_t process;  /* 0 */
    uint32_t flags;    /* 0 (unless weak bits) */
    uint32_t dat_chunk; /* id */
    uint32_t reserved[3];
};

/* ipf_img.flags */
#define IMGF_FLAKEY (1u<<0)

/* Density type codes */
enum dentype { denNoise=1, denUniform=2, denCopylock=3, denSpeedlock=6 };

struct ipf_data {
    uint32_t size;  /* ceil(bsize/8) */
    uint32_t bsize; /* # bits of encoded stream data */
    uint32_t dcrc;  /* data area crc */
    uint32_t dat_chunk; /* id */
    /* Followed by #blocks ipf_block structures */
};

struct ipf_block {
    uint32_t blockbits;  /* # raw MFM cells */
    uint32_t gapbits;    /* # raw MFM cells */
    union {
        struct {
            uint32_t blocksize;  /* ceil(blockbits/8) */
            uint32_t gapsize;    /* ceil(gapbits/8) */
        } caps;
        struct {
            uint32_t gapoffset;  /* 0 unless there is a gap stream */
            uint32_t celltype;   /* 1 for 2us MFM */
        } sps;
    } u;
    uint32_t enctype;    /* 1 */
    uint32_t flag;       /* 0 (bit 2 set => chunk counts are in bits) */
    uint32_t gapvalue;   /* 0 */
    uint32_t dataoffset; /* offset of data stream in data area */
    /* Data is a set of chunks */
    /* Chunk start bytes is count_len[7:5], code[4:0] (see below for codes) */
    /* count_len says how many following bytes contain (big endian) count */
};

/* Data stream chunk codes. */
enum chkcode { chkEnd=0, chkSync, chkData, chkGap, chkRaw, chkFlaky };

struct ipf_tbuf {
    struct tbuf tbuf;
    uint8_t *dat;
    unsigned int len, bits;
    unsigned int decoded_bits;
    unsigned int blockstart;
    unsigned int chunkstart, chunktype;
    unsigned int nr_blks, nr_sync;
    uint32_t encoder;
    bool_t need_sps_encoder;
    bool_t is_var_density;
    struct ipf_block *blk;
};

#define floor_bits_to_bytes(bits) ((bits)/8)
#define ceil_bits_to_bytes(bits) (((bits)+7)/8)

static void ipf_tbuf_finish_chunk(
    struct ipf_tbuf *ibuf, unsigned int new_chunktype)
{
    unsigned int chunklen, cntlen, i, j;

    chunklen = ibuf->len - ibuf->chunkstart;
    if (ibuf->encoder == ENC_SPS)
        chunklen = chunklen*8 + ibuf->bits;
    else if (ibuf->bits != 0)
        ibuf->need_sps_encoder = 1;

    if (ibuf->bits != 0) {
        ibuf->len++;
        ibuf->bits = 0;
    }

    if (chunklen == 0)
        goto out;

    if (ibuf->chunktype == chkFlaky)
        ibuf->len = ibuf->chunkstart;

    for (i = chunklen, cntlen = 0; i > 0; i >>= 8)
        cntlen++;
    memmove(&ibuf->dat[ibuf->chunkstart + 1 + cntlen],
            &ibuf->dat[ibuf->chunkstart], ibuf->len - ibuf->chunkstart);
    ibuf->dat[ibuf->chunkstart] = ibuf->chunktype | (cntlen << 5);
    for (i = chunklen, j = 0; i > 0; i >>= 8, j++)
        ibuf->dat[ibuf->chunkstart + cntlen - j] = (uint8_t)i;
    ibuf->len += 1 + cntlen;

    if ((new_chunktype == chkEnd) ||
        ((new_chunktype == chkSync) && ibuf->nr_sync++ &&
         !ibuf->tbuf.disable_auto_sector_split)) {
        struct ipf_block *blk = &ibuf->blk[ibuf->nr_blks++];
        blk->blockbits = ibuf->decoded_bits;
        blk->enctype = 1; /* MFM */
        blk->dataoffset = ibuf->blockstart;
        blk->gapvalue = ibuf->tbuf.gap_fill_byte;
        if (ibuf->encoder == ENC_CAPS) {
            blk->u.caps.blocksize = ceil_bits_to_bytes(blk->blockbits);
            blk->u.caps.gapsize = ceil_bits_to_bytes(blk->gapbits);
        } else {
            blk->u.sps.gapoffset = 0;
            blk->u.sps.celltype = 1; /* 2us bitcell */
            blk->flag = 4; /* bit-oriented */
        }
        ibuf->dat[ibuf->len++] = 0;
        ibuf->decoded_bits = 0;
        ibuf->blockstart = ibuf->len;
    }

out:
    ibuf->chunkstart = ibuf->len;
    ibuf->chunktype = new_chunktype;
}

static void ipf_tbuf_bit(
    struct tbuf *tbuf, uint16_t speed,
    enum bitcell_encoding enc, uint8_t dat)
{
    struct ipf_tbuf *ibuf = container_of(tbuf, struct ipf_tbuf, tbuf);
    unsigned int chunktype = (enc == bc_raw) ? chkSync : chkData;

    if (speed != SPEED_AVG)
        ibuf->is_var_density = 1;

    if (chunktype != ibuf->chunktype)
        ipf_tbuf_finish_chunk(ibuf, chunktype);

    ibuf->dat[ibuf->len] |= dat << (7 - ibuf->bits);
    ibuf->decoded_bits += (enc == bc_raw) ? 1 : 2;
    if (++ibuf->bits == 8) {
        ibuf->bits = 0;
        ibuf->len++;
    }
}

static void ipf_tbuf_gap(
    struct tbuf *tbuf, uint16_t speed, unsigned int bits)
{
    struct ipf_tbuf *ibuf = container_of(tbuf, struct ipf_tbuf, tbuf);
    struct ipf_block *blk = &ibuf->blk[ibuf->nr_blks];

    if (speed != SPEED_AVG)
        ibuf->is_var_density = 1;

    /* Store the gap size in block metadata. */
    blk->gapbits = bits*2;

    /* Prevent next sync mark from creating a new block. */
    ibuf->nr_sync = 0;

    /* This is both a chunk and a block boundary. */
    ipf_tbuf_finish_chunk(ibuf, chkEnd);
}

static void ipf_tbuf_weak(
    struct tbuf *tbuf, unsigned int bits)
{
    struct ipf_tbuf *ibuf = container_of(tbuf, struct ipf_tbuf, tbuf);

    ipf_tbuf_finish_chunk(ibuf, chkFlaky);
    ibuf->decoded_bits += 2*bits;
    ibuf->len += bits/8;
    ibuf->bits = bits&7;
}

static struct container *ipf_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static void ipf_write_chunk(
    struct disk *d, const char *id, const void *dat, size_t dat_len)
{
    struct ipf_header ipf_header;
    uint32_t crc, i, *_dat = memalloc(dat_len);

    for (i = 0; i < dat_len/4; i++)
        _dat[i] = htobe32(((uint32_t *)dat)[i]);

    memcpy(ipf_header.id, id, 4);
    ipf_header.len = htobe32(dat_len + sizeof(ipf_header));
    ipf_header.crc = 0;
    crc = crc32(&ipf_header, sizeof(ipf_header));
    crc = crc32_add(_dat, dat_len, crc);
    ipf_header.crc = htobe32(crc);

    write_exact(d->fd, &ipf_header, sizeof(ipf_header));
    write_exact(d->fd, _dat, dat_len);

    memfree(_dat);
}

static bool_t __ipf_close(struct disk *d, uint32_t encoder)
{
    time_t t;
    struct tm tm;
    struct ipf_info info;
    struct ipf_img *_img, *img;
    struct ipf_block *_blk, *blk;
    struct ipf_data *_idata, *idata;
    uint8_t *_dat, *dat;
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct ipf_tbuf ibuf;
    unsigned int i, j;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    ipf_write_chunk(d, "CAPS", NULL, 0);

    t = time(NULL);
    localtime_r(&t, &tm);
    if (tm.tm_sec > 59)
        tm.tm_sec = 59;

    memset(&info, 0, sizeof(info));
    info.type = 1; /* FDD */
    info.encoder = encoder;
    info.encrev = 1;
    info.release = info.revision = info.userid = IPF_ID;
    info.maxcyl = cyl(di->nr_tracks) - 1;
    info.maxhead = 1;
    info.date = (tm.tm_year+1900)*10000 + (tm.tm_mon+1)*100 + tm.tm_mday;
    info.time = tm.tm_hour*10000000 + tm.tm_min*100000 + tm.tm_sec*1000;
    info.platform[0] = 1; /* Amiga */
    ipf_write_chunk(d, "INFO", &info, sizeof(info));

    _img = img = memalloc(di->nr_tracks * sizeof(*img));
    _idata = idata = memalloc(di->nr_tracks * sizeof(*idata));
    _blk = blk = memalloc(di->nr_tracks * MAX_BLOCKS_PER_TRACK * sizeof(*blk));
    _dat = dat = memalloc(di->nr_tracks * MAX_DATA_PER_TRACK);

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];

        memset(&ibuf, 0, sizeof(ibuf));
        ibuf.encoder = encoder;

        if (((int)ti->total_bits < 0) && (i != 0) && d->kryoflux_hack) {
            /* Fill empty track from previous track. Fixes writeback to floppy
             * using DTC, which ignore single-sided and max-cyl parameters. */
            memcpy(img, img-1, sizeof(*img));
            memcpy(idata, idata-1, sizeof(*idata));
            ibuf.len = idata->size - img->blkcnt * sizeof(*blk);
            memcpy(dat, dat - ibuf.len, ibuf.len * sizeof(*dat));
            memcpy(blk, blk - img->blkcnt, img->blkcnt * sizeof(*blk));
        }

        img->cyl = i / 2;
        img->head = i & 1;
        img->sigtype = 1; /* 2us bitcell */
        idata->dat_chunk = img->dat_chunk = i + 1;

        if ((int)ti->total_bits < 0) {
            /* Unformatted tracks are handled by the IPF decoder library. */
            img->dentype = img->dentype ?: denNoise;
        } else {
            /* Basic track metadata. */
            img->dentype = 
                track_is_copylock(ti) ? denCopylock :
                (ti->type == TRKTYP_speedlock) ? denSpeedlock :
                denUniform;
            img->startbit = ti->data_bitoff - PREPEND_BITS;
            if ((int)img->startbit < 0)
                img->startbit += ti->total_bits;
            img->startpos = floor_bits_to_bytes(img->startbit);
            img->trkbits = ti->total_bits;
            img->trksize = ceil_bits_to_bytes(img->trkbits);

            /* Go get the encoded track data. */
            ibuf.tbuf.prng_seed = TBUF_PRNG_INIT;
            ibuf.tbuf.bit = ipf_tbuf_bit;
            ibuf.tbuf.gap = ipf_tbuf_gap;
            ibuf.tbuf.weak = ipf_tbuf_weak;
            ibuf.dat = dat;
            ibuf.blk = blk;
            ibuf.chunktype = chkGap;
            ibuf.decoded_bits = PREPEND_BITS;
            ibuf.len = ibuf.decoded_bits / 16;
            ibuf.bits = (ibuf.decoded_bits / 2) & 7;
            handlers[ti->type]->read_raw(d, i, &ibuf.tbuf);

            ipf_tbuf_finish_chunk(&ibuf, chkEnd);

            BUG_ON(ibuf.nr_blks > MAX_BLOCKS_PER_TRACK);
            BUG_ON(ibuf.len > MAX_DATA_PER_TRACK);

            if (ibuf.is_var_density && img->dentype == denUniform)
                trk_warn(ti, i, "IPF: unsupported variable density!");

            if (ibuf.need_sps_encoder) {
                BUG_ON(encoder != ENC_CAPS);
                warnx("IPF: Switching to SPS encoder.");
                goto out;
            }

            /* Sum the per-block data & gap sizes. */
            for (j = 0; j < ibuf.nr_blks; j++) {
                img->databits += blk[j].blockbits;
                img->gapbits += blk[j].gapbits;
                blk[j].dataoffset += ibuf.nr_blks * sizeof(*blk);
            }

            /* Track gap is appended to final block. */
            blk[j-1].gapbits += img->trkbits - img->databits - img->gapbits;
            if (encoder == ENC_CAPS)
                blk[j-1].u.caps.gapsize = ceil_bits_to_bytes(blk[j-1].gapbits);

            /* Finish the IMGE chunk. */
            img->gapbits = img->trkbits - img->databits;
            img->blkcnt = ibuf.nr_blks;
            if (ibuf.tbuf.raw.has_weak_bits)
                img->flags |= IMGF_FLAKEY;

            /* Convert endianness of all block descriptors. */
            for (j = 0; j < img->blkcnt * sizeof(*blk) / 4; j++)
                ((uint32_t *)blk)[j] = htobe32(((uint32_t *)blk)[j]);

            /* Finally, compute DATA CRC. */
            idata->size = ibuf.len + ibuf.nr_blks * sizeof(*blk);
            idata->bsize = idata->size * 8;
            idata->dcrc = crc32(blk, ibuf.nr_blks * sizeof(*blk));
            idata->dcrc = crc32_add(dat, ibuf.len, idata->dcrc);
        }

        /* We write the IMGE chunks back-to-back; defer DATA until after. */
        ipf_write_chunk(d, "IMGE", img, sizeof(*img));

        dat += ibuf.len;
        blk += img->blkcnt;
        idata++; img++;
    }

    idata = _idata;
    img = _img;
    blk = _blk;
    dat = _dat;
    for (i = 0; i < di->nr_tracks; i++) {
        ipf_write_chunk(d, "DATA", idata, sizeof(*idata));
        write_exact(d->fd, blk, img->blkcnt * sizeof(*blk));
        write_exact(d->fd, dat, idata->size - img->blkcnt * sizeof(*blk));
        dat += idata->size - img->blkcnt * sizeof(*blk);
        blk += img->blkcnt;
        idata++; img++;
    }

out:
    memfree(_img);
    memfree(_idata);
    memfree(_blk);
    memfree(_dat);
    return i == di->nr_tracks; /* success? */
}

static void ipf_close(struct disk *d)
{
    /* Try the older CAPS encoding, and use the newer SPS encoding only when we
     * discover it is necessary. Note that the new encoding does not work with
     * v2 of the IPF decoder library (e.g., libcapsimage.so.2 on Linux). An
     * upgrade to the latest decoder library (v4.2 or later) is recommended. */
    if (!__ipf_close(d, ENC_CAPS) &&
        !__ipf_close(d, ENC_SPS))
        BUG();
}

struct container container_ipf = {
    .init = dsk_init,
    .open = ipf_open,
    .close = ipf_close,
    .write_raw = dsk_write_raw
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
