/*
 * libdisk/container_dsk.c
 * 
 * Read/write DSK images.
 * Also write-only IPF support, piggy-backing on the DSK routines.
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

/*****
 * DSK
 * 
 * On-disk Format:
 *  <struct disk_header>
 *  <struct track_header> * #tracks (each entry is disk_header.bytes_per_thdr)
 *  [<struct tag_header> tag data...]+
 *  <track data...>
 * All fields are big endian (network ordering).
 */

struct disk_header {
    char signature[4];
    uint16_t version;
    uint16_t nr_tracks;
    uint16_t bytes_per_thdr;
    uint16_t flags;
};

struct track_header {
    /* Enumeration */
    uint16_t type;

    uint16_t flags;

    /* Bitmap of valid sectors. */
    uint32_t valid_sectors;

    /* Offset and length of type-specific track data in container file. */
    uint32_t off;
    uint32_t len;

    /*
     * Offset from track index of raw data returned by type handler.
     * Specifically, N means that the there are N full bitcells between the
     * index pulse and the first data bitcell. Hence 0 means that the index
     * pulse occurs during the cell immediately preceding the first data cell.
     */
    uint32_t data_bitoff;

    /*
     * Total bit length of track (modulo jitter at the write splice / gap).
     * If TRK_WEAK then handler can be called repeatedly for successive
     * revolutions of the disk -- data and length may change due to 'flakey
     * bits' which confuse the disk controller.
     */
    uint32_t total_bits;
};

struct tag_header {
    uint16_t id;
    uint16_t len;
};

static void tag_swizzle(struct disk_tag *dtag)
{
    switch (dtag->id) {
    case DSKTAG_rnc_pdos_key: {
        struct rnc_pdos_key *t = (struct rnc_pdos_key *)dtag;
        t->key = ntohl(t->key);
        break;
    }
    }
}

static void dsk_init(struct disk *d)
{
    struct track_info *ti;
    struct disk_info *di;
    unsigned int i, nr_tracks = 160;

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = nr_tracks;
    di->flags = 0;
    di->track = memalloc(nr_tracks * sizeof(*ti));

    for (i = 0; i < nr_tracks; i++) {
        ti = &di->track[i];
        memset(ti, 0, sizeof(*ti));
        init_track_info(ti, TRKTYP_unformatted);
        ti->total_bits = TRK_WEAK;
    }

    d->tags = memalloc(sizeof(*d->tags));
    d->tags->tag.id = DSKTAG_end;
}

static int dsk_open(struct disk *d)
{
    struct disk_header dh;
    struct track_header th;
    struct tag_header tagh;
    struct disk_list_tag *dltag, **pprevtag;
    struct disk_tag *dtag;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, bytes_per_th, read_bytes_per_th;
    off_t off;

    read_exact(d->fd, &dh, sizeof(dh));
    if (strncmp(dh.signature, "DSK\0", 4) ||
        (ntohs(dh.version) != 0))
        return 0;

    di = memalloc(sizeof(*di));
    di->nr_tracks = ntohs(dh.nr_tracks);
    di->flags = ntohs(dh.flags);
    di->track = memalloc(di->nr_tracks * sizeof(*ti));
    read_bytes_per_th = bytes_per_th = ntohs(dh.bytes_per_thdr);
    if (read_bytes_per_th > sizeof(*ti))
        read_bytes_per_th = sizeof(*ti);

    for (i = 0; i < di->nr_tracks; i++) {
        memset(&th, 0, sizeof(th));
        read_exact(d->fd, &th, read_bytes_per_th);
        ti = &di->track[i];
        init_track_info(ti, ntohs(th.type));
        ti->flags = ntohs(th.flags);
        ti->valid_sectors = ntohl(th.valid_sectors);
        ti->len = ntohl(th.len);
        ti->data_bitoff = ntohl(th.data_bitoff);
        ti->total_bits = ntohl(th.total_bits);
        off = lseek(d->fd, bytes_per_th-read_bytes_per_th, SEEK_CUR);
        lseek(d->fd, ntohl(th.off), SEEK_SET);
        ti->dat = memalloc(ti->len);
        read_exact(d->fd, ti->dat, ti->len);
        lseek(d->fd, off, SEEK_SET);
    }

    pprevtag = &d->tags;
    do {
        read_exact(d->fd, &tagh, sizeof(tagh));
        dltag = memalloc(sizeof(*dltag) + ntohs(tagh.len));
        dtag = &dltag->tag;
        dtag->id = ntohs(tagh.id);
        dtag->len = ntohs(tagh.len);
        read_exact(d->fd, dtag+1, dtag->len);
        tag_swizzle(dtag);
        *pprevtag = dltag;
        pprevtag = &dltag->next;
    } while (dtag->id != DSKTAG_end);
    *pprevtag = NULL;

    d->di = di;
    return 1;
}

static void dsk_close(struct disk *d)
{
    struct disk_header dh;
    struct track_header th;
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct disk_list_tag *dltag;
    struct disk_tag *dtag;
    unsigned int i, datoff;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    strncpy(dh.signature, "DSK\0", 4);
    dh.version = 0;
    dh.nr_tracks = htons(di->nr_tracks);
    dh.bytes_per_thdr = htons(sizeof(th));
    dh.flags = htons(di->flags);
    write_exact(d->fd, &dh, sizeof(dh));

    datoff = sizeof(dh) + di->nr_tracks * sizeof(th);
    for (dltag = d->tags; dltag != NULL; dltag = dltag->next)
        datoff += sizeof(struct tag_header) + dltag->tag.len;

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        th.type = htons(ti->type);
        th.flags = htons(ti->flags);
        th.valid_sectors = htonl(ti->valid_sectors);
        th.off = htonl(datoff);
        th.len = htonl(ti->len);
        th.data_bitoff = htonl(ti->data_bitoff);
        th.total_bits = htonl(ti->total_bits);
        write_exact(d->fd, &th, sizeof(th));
        datoff += ti->len;
    }

    for (dltag = d->tags; dltag != NULL; dltag = dltag->next) {
        struct tag_header tagh;
        dtag = &dltag->tag;
        tagh.id = htons(dtag->id);
        tagh.len = htons(dtag->len);
        tag_swizzle(dtag);
        write_exact(d->fd, &tagh, sizeof(tagh));
        write_exact(d->fd, dtag+1, dtag->len);
        tag_swizzle(dtag);
    }

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->len != 0)
            write_exact(d->fd, ti->dat, ti->len);
    }
}

static int dsk_write_mfm(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    memset(ti, 0, sizeof(*ti));
    init_track_info(ti, type);
    ti->total_bits = DEFAULT_BITS_PER_TRACK;
    stream_reset(s, tracknr);
    stream_next_index(s);
    ti->dat = handlers[type]->write_mfm(d, tracknr, s);

    if (ti->dat == NULL) {
        memset(ti, 0, sizeof(*ti));
        init_track_info(ti, TRKTYP_unformatted);
        ti->typename = "Unformatted*";
        ti->total_bits = TRK_WEAK;
        return -1;
    }

    if (ti->total_bits == 0) {
        stream_reset(s, tracknr);
        stream_next_index(s);
        stream_next_index(s);
        ti->total_bits = s->track_bitlen ? : DEFAULT_BITS_PER_TRACK;
    }

    ti->data_bitoff = (int32_t)ti->data_bitoff % (int32_t)ti->total_bits;
    if ((int32_t)ti->data_bitoff < 0)
        ti->data_bitoff += ti->total_bits;

    return 0;
}

struct container container_dsk = {
    .init = dsk_init,
    .open = dsk_open,
    .close = dsk_close,
    .write_mfm = dsk_write_mfm
};


/*****
 * IPF
 */

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

static void ipf_tbuf_finalise_block(
    struct ipf_tbuf *ibuf, unsigned int new_chunktype)
{
    unsigned int chunklen, cntlen, i, j;

    if ((int)ibuf->chunktype >= 0) {
        chunklen = ibuf->len - ibuf->chunkstart;
        for (i = chunklen, cntlen = 0; i > 0; i >>= 8)
            cntlen++;
        memmove(&ibuf->dat[ibuf->chunkstart + 1 + cntlen],
                &ibuf->dat[ibuf->chunkstart], chunklen);
        ibuf->dat[ibuf->chunkstart] = ibuf->chunktype | (cntlen << 5);
        for (i = chunklen, j = 0; i > 0; i >>= 8, j++)
            ibuf->dat[ibuf->chunkstart + cntlen - j] = (uint8_t)i;
        ibuf->len += 1 + cntlen;
    }

    if ((new_chunktype == 0) ||
        ((new_chunktype == 1) && ((int)ibuf->chunktype >= 0))) {
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
        ipf_tbuf_finalise_block(ibuf, chunktype);

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
            ibuf.chunktype = ~0u;
            handlers[ti->type]->read_mfm(d, i, &ibuf.tbuf);
            ipf_tbuf_finalise_block(&ibuf, 0);

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
