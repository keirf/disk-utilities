/*
 * libdisk/container_ipf.c
 * 
 * Write-only SPS/CAPS IPF support.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

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
    uint32_t dentype;  /* 1 = noise, 2 = auto, 3 = copylock */
    uint32_t sigtype;  /* 1 */
    uint32_t trksize;  /* round_up(trkbits) */
    uint32_t startpos; /* round_up(startbit) */
    uint32_t startbit; /* bit offset from index of data start */
    uint32_t databits; /* # raw MFM cells */
    uint32_t gapbits;  /* # raw MFM cells */
    uint32_t trkbits;  /* databits + gapbits */
    uint32_t blkcnt;   /* e.g., 11 for DOS */
    uint32_t process;  /* 0 */
    uint32_t flag;     /* 0 (unless weak bits) */
    uint32_t dat_chunk; /* id */
    uint32_t reserved[3];
};

struct ipf_data {
    uint32_t size;  /* round_up(bsize) */
    uint32_t bsize; /* # bits of encoded stream data */
    uint32_t dcrc;  /* data area crc */
    uint32_t dat_chunk; /* id */
    /* Followed by #blocks ipf_block structures */
};

struct ipf_block {
    uint32_t blockbits;  /* # raw MFM cells */
    uint32_t gapbits;    /* # raw MFM cells (0 for us) */
    uint32_t blocksize;  /* round_up(blockbits) */
    uint32_t gapsize;    /* round_up(gapbits) */
    uint32_t enctype;    /* 1 */
    uint32_t flag;       /* 0 */
    uint32_t gapvalue;   /* 0 */
    uint32_t dataoffset; /* offset of data stream in data area */
    /* Data is a set of chunks */
    /* Chunk start bytes is count_len[7:5], code[4:0] */
    /* count_len says how many following bytes contain (big endian) count */
    /* code is 0=end,1=sync,2=data,3=gap,4=raw,5=flakey */
};

struct ipf_tbuf {
    struct track_buffer tbuf;
    uint8_t *dat;
    unsigned int len, bits;
    unsigned int decoded_len;
    unsigned int blockstart;
    unsigned int chunkstart, chunktype;
    unsigned int nr_blks;
    struct ipf_block *blk;
};

static void ipf_tbuf_finish_chunk(
    struct ipf_tbuf *ibuf, unsigned int new_chunktype)
{
    unsigned int chunklen, cntlen, i, j;

    chunklen = ibuf->len - ibuf->chunkstart;
    for (i = chunklen, cntlen = 0; i > 0; i >>= 8)
        cntlen++;
    memmove(&ibuf->dat[ibuf->chunkstart + 1 + cntlen],
            &ibuf->dat[ibuf->chunkstart], chunklen);
    ibuf->dat[ibuf->chunkstart] = ibuf->chunktype | (cntlen << 5);
    for (i = chunklen, j = 0; i > 0; i >>= 8, j++)
        ibuf->dat[ibuf->chunkstart + cntlen - j] = (uint8_t)i;
    ibuf->len += 1 + cntlen;

    if (new_chunktype != 2) {
        struct ipf_block *blk = &ibuf->blk[ibuf->nr_blks++];
        blk->blocksize = ibuf->decoded_len;
        blk->blockbits = blk->blocksize * 8;
        blk->enctype = 1;
        blk->dataoffset = ibuf->blockstart;
        ibuf->dat[ibuf->len++] = 0;
        ibuf->decoded_len = 0;
        ibuf->blockstart = ibuf->len;
    }

    ibuf->chunkstart = ibuf->len;
    ibuf->chunktype = new_chunktype;
    ibuf->bits = 0;
}

static void ipf_tbuf_byte(
    struct track_buffer *tbuf, uint16_t speed,
    enum tbuf_data_type type, uint8_t x)
{
    struct ipf_tbuf *ibuf = container_of(tbuf, struct ipf_tbuf, tbuf);
    unsigned int i, j, chunktype = (type == TB_raw) ? 1 : 2;

    if (chunktype != ibuf->chunktype)
        ipf_tbuf_finish_chunk(ibuf, chunktype);

    if ((type == TB_raw) || (type == TB_all)) {
        ibuf->dat[ibuf->len++] = x;
        if (type == TB_all)
            ibuf->decoded_len++;
    } else {
        if (type == TB_even)
            x >>= 1;
        for (i = 0, j = 0; i < 4; i++)
            j |= ((x >> (i << 1)) & 1) << i;
        if (ibuf->bits == 0)
            ibuf->dat[ibuf->len] = (uint8_t)(j << 4);
        else
            ibuf->dat[ibuf->len++] |= (uint8_t)j;
        ibuf->bits ^= 4;
    }

    ibuf->decoded_len++;
}

static int ipf_open(struct disk *d)
{
    /* not supported */
    return 0;
}

static void ipf_write_chunk(
    struct disk *d, const char *id, const void *dat, size_t dat_len)
{
    struct ipf_header ipf_header;
    uint32_t crc, i, *_dat = memalloc(dat_len);

    for (i = 0; i < dat_len/4; i++)
        _dat[i] = htonl(((uint32_t *)dat)[i]);

    memcpy(ipf_header.id, id, 4);
    ipf_header.len = htonl(dat_len + sizeof(ipf_header));
    ipf_header.crc = 0;
    crc = crc32(&ipf_header, sizeof(ipf_header));
    crc = crc32_add(_dat, dat_len, crc);
    ipf_header.crc = htonl(crc);

    write_exact(d->fd, &ipf_header, sizeof(ipf_header));
    write_exact(d->fd, _dat, dat_len);

    memfree(_dat);
}

static void ipf_close(struct disk *d)
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
    info.encoder = info.encrev = 1; /* CAPS */
    info.release = 0x6666; /* bogus */
    info.revision = 1;
    info.maxcyl = di->nr_tracks/2 - 1;
    info.maxhead = 1;
    info.date = (tm.tm_year+1900)*10000 + (tm.tm_mon+1)*100 + tm.tm_mday;
    info.time = tm.tm_hour*10000000 + tm.tm_min*100000 + tm.tm_sec*1000;
    info.platform[0] = 1; /* Amiga */
    ipf_write_chunk(d, "INFO", &info, sizeof(info));

    _img = img = memalloc(di->nr_tracks * sizeof(*img));
    _idata = idata = memalloc(di->nr_tracks * sizeof(*idata));
    _blk = blk = memalloc(di->nr_tracks * 20 * sizeof(*blk));
    _dat = dat = memalloc(di->nr_tracks * 20 * 1024);

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        memset(&ibuf, 0, sizeof(ibuf));
        img->cyl = i / 2;
        img->head = i & 1;
        img->sigtype = 1; /* 2us bitcell */
        idata->dat_chunk = img->dat_chunk = i + 1;
        if ((int)ti->total_bits < 0) {
            img->dentype = 1; /* noise */
        } else {
            img->dentype = 2; /* auto */
            if (ti->type == TRKTYP_copylock)
                img->dentype = 3; /* copylock */
            img->startbit = ti->data_bitoff;
            img->startpos = img->startbit / 8;
            img->trkbits = ti->total_bits;
            img->trksize = img->trkbits / 8;

            ibuf.tbuf.byte = ipf_tbuf_byte;
            ibuf.dat = dat;
            ibuf.blk = blk;
            /* Start with 4 bytes of track gap MFM 0xAAAAAAAA */
            ibuf.chunktype = 1;
            *(uint32_t *)ibuf.dat = 0xaaaaaaaa;
            ibuf.len = ibuf.decoded_len = 4;
            handlers[ti->type]->read_mfm(d, i, &ibuf.tbuf);
            ipf_tbuf_finish_chunk(&ibuf, 0);

            for (j = 0; j < ibuf.nr_blks; j++) {
                img->databits += blk[j].blockbits;
                blk[j].dataoffset += ibuf.nr_blks * sizeof(*blk);
            }
            img->gapbits = img->trkbits - img->databits;
            img->blkcnt = ibuf.nr_blks;

            for (j = 0; j < img->blkcnt * sizeof(*blk) / 4; j++)
                ((uint32_t *)blk)[j] = htonl(((uint32_t *)blk)[j]);

            idata->size = ibuf.len + ibuf.nr_blks * sizeof(*blk);
            idata->bsize = idata->size * 8;
            idata->dcrc = crc32(blk, ibuf.nr_blks * sizeof(*blk));
            idata->dcrc = crc32_add(dat, ibuf.len, idata->dcrc);
        }
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

    memfree(_img);
    memfree(_idata);
    memfree(_blk);
    memfree(_dat);
}

struct container container_ipf = {
    .init = dsk_init,
    .open = ipf_open,
    .close = ipf_close,
    .write_mfm = dsk_write_mfm
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
