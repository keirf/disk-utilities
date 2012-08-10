/*
 * libdisk/disk.c
 * 
 * Framework for container types and track-format handlers.
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

#define X(a,b) extern struct track_handler a##_handler;
#include <libdisk/track_types.h>
#undef X

const struct track_handler *handlers[] = {
#define X(a,b) &a##_handler,
#include <libdisk/track_types.h>
#undef X
    NULL
};

static struct track_format_names {
    const char *id_name;
    const char *desc_name;
} track_format_names[] = {
#define X(a,b) {#a,b},
#include <libdisk/track_types.h>
#undef X
};

static void tbuf_finalise(struct track_buffer *tbuf);

static struct container *container_from_filename(
    const char *name)
{
    const char *p = strrchr(name, '.');
    if (p == NULL)
        goto fail;
    p++;
    if (!strcmp(p, "adf"))
        return &container_adf;
    if (!strcmp(p, "eadf"))
        return &container_eadf;
    if (!strcmp(p, "dsk"))
        return &container_dsk;
    if (!strcmp(p, "img"))
        return &container_img;
    if (!strcmp(p, "ipf"))
        return &container_ipf;
fail:
    warnx("Unknown file suffix: %s (valid suffixes: .adf,.dsk,.ipf)", name);
    return NULL;
}

struct disk *disk_create(const char *name)
{
    struct disk *d;
    struct container *c;
    int fd;

    if ((c = container_from_filename(name)) == NULL)
        return NULL;

    if ((fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1) {
        warn("%s", name);
        return NULL;
    }

    d = memalloc(sizeof(*d));
    d->fd = fd;
    d->read_only = 0;
    d->container = c;

    c->init(d);

    return d;
}

struct disk *disk_open(const char *name, int read_only)
{
    struct disk *d;
    struct container *c;
    int fd;

    if ((c = container_from_filename(name)) == NULL)
        return NULL;

    if ((fd = open(name, read_only ? O_RDONLY : O_RDWR)) == -1) {
        warn("%s", name);
        return NULL;
    }

    d = memalloc(sizeof(*d));
    d->fd = fd;
    d->read_only = read_only;
    d->container = c->open(d);

    if (!d->container) {
        warnx("%s: Bad disk image", name);
        memfree(d);
        return NULL;
    }

    return d;
}

void disk_close(struct disk *d)
{
    struct disk_list_tag *dltag;
    struct disk_info *di = d->di;
    unsigned int i;

    if (!d->read_only)
        d->container->close(d);

    dltag = d->tags;
    while (dltag != NULL) {
        struct disk_list_tag *nxt = dltag->next;
        memfree(dltag);
        dltag = nxt;
    }

    for (i = 0; i < di->nr_tracks; i++)
        memfree(di->track[i].dat);
    memfree(di->track);
    memfree(di);
    close(d->fd);
    memfree(d);
}

struct disk_info *disk_get_info(struct disk *d)
{
    return d->di;
}

struct track_mfm *track_mfm_get(struct disk *d, unsigned int tracknr)
{
    struct disk_info *di = d->di;
    struct track_info *ti;
    const struct track_handler *thnd;
    struct track_mfm *track_mfm;
    struct track_buffer tbuf;

    if (tracknr >= di->nr_tracks)
        return NULL;
    ti = &di->track[tracknr];

    if ((int32_t)ti->total_bits > 0)
        tbuf_init(&tbuf, ti->data_bitoff, ti->total_bits);

    thnd = handlers[ti->type];
    thnd->read_mfm(d, tracknr, &tbuf);

    tbuf_finalise(&tbuf);

    track_mfm = memalloc(sizeof(*track_mfm));
    track_mfm->mfm = tbuf.mfm;
    track_mfm->speed = tbuf.speed;
    track_mfm->bitlen = tbuf.len;
    track_mfm->has_weak_bits = tbuf.has_weak_bits;
    return track_mfm;
}

void track_mfm_put(struct track_mfm *track_mfm)
{
    if (track_mfm == NULL)
        return;
    memfree(track_mfm->mfm);
    memfree(track_mfm->speed);
    memfree(track_mfm);
}

int track_write_mfm_from_stream(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    memfree(ti->dat);
    ti->dat = NULL;

    return d->container->write_mfm(d, tracknr, type, s);
}

int track_write_mfm(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct track_mfm *mfm)
{
    struct stream *s = stream_soft_open(mfm->mfm, mfm->speed, mfm->bitlen);
    int rc = track_write_mfm_from_stream(d, tracknr, type, s);
    stream_close(s);
    return rc;
}

void track_mark_unformatted(
    struct disk *d, unsigned int tracknr)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    memfree(ti->dat);
    memset(ti, 0, sizeof(*ti));
    init_track_info(ti, TRKTYP_unformatted);
    ti->total_bits = TRK_WEAK;
}

struct disk_tag *disk_get_tag_by_id(struct disk *d, uint16_t id)
{
    struct disk_list_tag *dltag;
    for (dltag = d->tags; dltag != NULL; dltag = dltag->next)
        if (dltag->tag.id == id)
            return &dltag->tag;
    return NULL;
}

struct disk_tag *disk_get_tag_by_idx(struct disk *d, unsigned int idx)
{
    struct disk_list_tag *dltag;
    unsigned int i;
    for (dltag = d->tags, i = 0;
         (dltag != NULL) && (i < idx);
         dltag = dltag->next, i++)
        continue;
    return dltag ? &dltag->tag : NULL;
}

struct disk_tag *disk_set_tag(
    struct disk *d, uint16_t id, uint16_t len, void *dat)
{
    struct disk_list_tag *dltag, **pprev;

    dltag = memalloc(sizeof(*dltag) + len);
    dltag->tag.id = id;
    dltag->tag.len = len;
    memcpy(&dltag->tag + 1, dat, len);

    for (pprev = &d->tags; *pprev != NULL; pprev = &(*pprev)->next) {
        struct disk_list_tag *cur = *pprev;
        if (cur->tag.id < id)
            continue;
        dltag->next = cur;
        *pprev = dltag;
        if (cur->tag.id == id) {
            dltag->next = cur->next;
            memfree(cur);
        }
        break;
    }

    return &dltag->tag;
}

const char *disk_get_format_id_name(enum track_type type)
{
    if (type >= ARRAY_SIZE(track_format_names))
        return NULL;
    return track_format_names[type].id_name;
}

const char *disk_get_format_desc_name(enum track_type type)
{
    if (type >= ARRAY_SIZE(track_format_names))
        return NULL;
    return track_format_names[type].desc_name;
}


/*
 * PRIVATE HELPERS
 */

void init_track_info(struct track_info *ti, enum track_type type)
{
    const struct track_handler *thnd = handlers[type];
    ti->type = type;
    ti->typename = track_format_names[type].desc_name;
    ti->bytes_per_sector = thnd->bytes_per_sector;
    ti->nr_sectors = thnd->nr_sectors;
    ti->len = ti->bytes_per_sector * ti->nr_sectors;
}

static void change_bit(uint8_t *map, unsigned int bit, bool_t on)
{
    if (on)
        map[bit>>3] |= 0x80 >> (bit & 7);
    else
        map[bit>>3] &= ~(0x80 >> (bit & 7));
}

static void append_bit(struct track_buffer *tbuf, uint16_t speed, uint8_t x)
{
    change_bit(tbuf->mfm, tbuf->pos, x);
    tbuf->speed[tbuf->pos >> 3] = speed;
    if (++tbuf->pos >= tbuf->len)
        tbuf->pos = 0;
}

static void mfm_tbuf_bit(
    struct track_buffer *tbuf, uint16_t speed,
    enum mfm_encoding enc, uint8_t dat)
{
    if (enc == MFM_all) {
        /* Clock bit */
        uint8_t clk = !(tbuf->prev_data_bit | dat);
        append_bit(tbuf, speed, clk);
    }

    /* Data bit */
    append_bit(tbuf, speed, dat);
    tbuf->prev_data_bit = dat;
}

void tbuf_init(struct track_buffer *tbuf, uint32_t bitstart, uint32_t bitlen)
{
    unsigned int bytes = bitlen + 7 / 8;
    memset(tbuf, 0, sizeof(*tbuf));
    tbuf->start = tbuf->pos = bitstart;
    tbuf->len = bitlen;
    tbuf->mfm = memalloc(bytes);
    tbuf->speed = memalloc(2*bytes);
    tbuf->bit = mfm_tbuf_bit;
}

static void tbuf_finalise(struct track_buffer *tbuf)
{
    int32_t pos, nr_bits;
    uint8_t b = 0;

    if (tbuf->start == tbuf->pos)
        return; /* handler completely filled the buffer */

    /* Forward fill half the gap */
    nr_bits = tbuf->start - tbuf->pos;
    if (nr_bits < 0)
        nr_bits += tbuf->len;
    nr_bits /= 4; /* /2 to halve the gap, /2 to count data bits only */
    while (nr_bits--)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 1, 0);

    /* Write splice. Write an MFM-illegal string of zeroes. */
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 5, 0);

    /* Reverse fill the remainder */
    pos = tbuf->start;
    do {
        if (--pos < 0)
            pos += tbuf->len;
        change_bit(tbuf->mfm, pos, b);
        tbuf->speed[pos >> 3] = SPEED_AVG;
        b = !b;
    } while (pos != tbuf->pos);
}

void tbuf_bits(struct track_buffer *tbuf, uint16_t speed,
               enum mfm_encoding enc, unsigned int bits, uint32_t x)
{
    int i;

    if (enc == MFM_even_odd) {
        tbuf_bits(tbuf, speed, MFM_even, bits, x);
        enc = MFM_odd;
    } else if (enc == MFM_odd_even) {
        tbuf_bits(tbuf, speed, MFM_odd, bits, x);
        enc = MFM_even;
    }

    if ((enc == MFM_even) || (enc == MFM_odd)) {
        uint32_t y = 0;
        if (enc == MFM_even)
            x >>= 1;
        bits >>= 1;
        for (i = 0; i < bits; i++)
            y |= (x >> i) & (1u << i);
        x = y;
        enc = MFM_all;
    }

    for (i = bits-1; i >= 0; i--) {
        uint8_t b = (x >> i) & 1;
        if ((enc != MFM_raw) || !(i & 1))
            tbuf->crc16_ccitt = crc16_ccitt_bit(b, tbuf->crc16_ccitt);
        tbuf->bit(tbuf, speed, enc, b);
    }
}

void tbuf_bytes(struct track_buffer *tbuf, uint16_t speed,
                enum mfm_encoding enc, unsigned int bytes, void *data)
{
    unsigned int i;
    uint8_t *p;

    if (enc == MFM_even_odd) {
        tbuf_bytes(tbuf, speed, MFM_even, bytes, data);
        enc = MFM_odd;
    } else if (enc == MFM_odd_even) {
        tbuf_bytes(tbuf, speed, MFM_odd, bytes, data);
        enc = MFM_even;
    }

    p = (uint8_t *)data;
    for (i = 0; i < bytes; i++)
        tbuf_bits(tbuf, speed, enc, 8, p[i]);
}

void tbuf_gap(struct track_buffer *tbuf, uint16_t speed, unsigned int bits)
{
    if (tbuf->gap != NULL) {
        tbuf->gap(tbuf, speed, bits);
    } else {
        while (bits--)
            tbuf->bit(tbuf, speed, MFM_all, 0);
    }
}

void tbuf_weak(struct track_buffer *tbuf, uint16_t speed, unsigned int bits)
{
    tbuf->has_weak_bits = 1;
    if (tbuf->weak != NULL) {
        tbuf->weak(tbuf, speed, bits);
    } else {
        static unsigned int seed = 0;
        while (bits--)
            tbuf->bit(tbuf, speed, MFM_all, rand_r(&seed) & 1);
    }
}

void tbuf_start_crc(struct track_buffer *tbuf)
{
    tbuf->crc16_ccitt = 0xffff;
}

void tbuf_emit_crc16_ccitt(struct track_buffer *tbuf, uint16_t speed)
{
    tbuf_bits(tbuf, speed, MFM_all, 16, tbuf->crc16_ccitt);
}

uint32_t mfm_decode_bits(enum mfm_encoding enc, uint32_t x)
{
    if (enc == MFM_all) {
        uint32_t i, y = 0;
        for (i = 0; i < 16; i++) {
            y |= (x & 1) << i;
            x >>= 2;
        }
        return y;
    } 

    if (enc == MFM_even)
        return (x & 0x55555555u) << 1;

    if (enc == MFM_odd)
        return x & 0x55555555u;

    BUG_ON(enc != MFM_raw);
    return x;
}

void mfm_decode_bytes(
    enum mfm_encoding enc, unsigned int bytes, void *in, void *out)
{
    uint8_t *in_b = in, *out_b = out;
    unsigned int i;

    for (i = 0; i < bytes; i++) {
        if (enc == MFM_all) {
            out_b[i] = mfm_decode_bits(MFM_all, ntohs(((uint16_t *)in)[i]));
        } else if (enc == MFM_even_odd) {
            out_b[i] = (mfm_decode_bits(MFM_even, in_b[i]) |
                        mfm_decode_bits(MFM_odd, in_b[i + bytes]));
        } else if (enc == MFM_odd_even) {
            out_b[i] = (mfm_decode_bits(MFM_odd, in_b[i]) |
                        mfm_decode_bits(MFM_even, in_b[i + bytes]));
        } else {
            BUG();
        }
    }
}

uint32_t mfm_encode_word(uint32_t w)
{
    uint32_t i, d, p = (w >> 16) & 1, x = 0;
    for (i = 0; i < 16; i++) {
        d = !!(w & 0x8000u);
        x = (x << 2) | (!(d|p) << 1) | d;
        p = d;
        w <<= 1;
    }
    return x;
}

uint32_t amigados_checksum(void *dat, unsigned int bytes)
{
    uint32_t *p = dat, csum = 0;
    unsigned int i;
    for (i = 0; i < bytes/4; i++)
        csum ^= ntohl(p[i]);
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    return csum;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
