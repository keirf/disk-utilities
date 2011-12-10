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
    const char *p = name + strlen(name) - 4;
    if (p < name)
        goto fail;
    if (!strcmp(p, ".adf"))
        return &container_adf;
    if (!strcmp(p, ".dsk"))
        return &container_dsk;
fail:
    warnx("Unknown file suffix: %s (valid suffixes: .adf,.dsk)", name);
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
    int fd, rc;

    if ((c = container_from_filename(name)) == NULL)
        return NULL;

    if ((fd = open(name, read_only ? O_RDONLY : O_RDWR)) == -1) {
        warn("%s", name);
        return NULL;
    }

    d = memalloc(sizeof(*d));
    d->fd = fd;
    d->read_only = read_only;
    d->container = c;

    rc = c->open(d);
    if (!rc) {
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
    struct track_info *ti = &di->track[tracknr];
    const struct track_handler *thnd;
    struct track_mfm *track_mfm;
    struct track_buffer tbuf = {
        .start = ti->data_bitoff,
        .len = ti->total_bits
    };

    if ((int32_t)tbuf.len > 0)
        tbuf_init(&tbuf);

    thnd = handlers[ti->type];
    thnd->read_mfm(d, tracknr, &tbuf);

    tbuf_finalise(&tbuf);

    track_mfm = memalloc(sizeof(*track_mfm));
    track_mfm->mfm = tbuf.mfm;
    track_mfm->speed = tbuf.speed;
    track_mfm->bitlen = tbuf.len;
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

void tbuf_init(struct track_buffer *tbuf)
{
    unsigned int bytes = tbuf->len + 7 / 8;
    tbuf->pos = tbuf->start;
    tbuf->mfm = memalloc(bytes);
    tbuf->speed = memalloc(2*bytes);
    tbuf->prev_data_bit = 0;
}

static void change_bit(uint8_t *map, unsigned int bit, bool_t on)
{
    if (on)
        map[bit>>3] |= 0x80 >> (bit & 7);
    else
        map[bit>>3] &= ~(0x80 >> (bit & 7));
}

static void tbuf_finalise(struct track_buffer *tbuf)
{
    int32_t pos;
    uint8_t b = 0;

    if (tbuf->start == tbuf->pos)
        return; /* handler completely filled the buffer */

    tbuf_bits(tbuf, SPEED_AVG, TB_all, 32, 0);

    pos = tbuf->start;
    for (;;) {
        if (--pos < 0)
            pos += tbuf->len;
        if (pos == tbuf->pos)
            break;
        change_bit(tbuf->mfm, pos, b);
        tbuf->speed[pos >> 3] = SPEED_AVG;
        b = !b;
    }
}

void tbuf_bits(struct track_buffer *tbuf, uint16_t speed,
               enum tbuf_data_type type, unsigned int bits, uint32_t x)
{
    unsigned int i;

    if (type == TB_even_odd) {
        tbuf_bits(tbuf, speed, TB_even, bits, x);
        type = TB_odd;
    } else if (type == TB_odd_even) {
        tbuf_bits(tbuf, speed, TB_odd, bits, x);
        type = TB_even;
    }

    if (bits != 8) {
        bits >>= 1;
        tbuf_bits(tbuf, speed, type, bits, x>>bits);
        tbuf_bits(tbuf, speed, type, bits, x);
        return;
    }

    if (type == TB_raw) {
        for (i = 0; i < 8; i++) {
            uint8_t b = !!((x << i) & 0x80);
            tbuf->prev_data_bit = b;
            change_bit(tbuf->mfm, tbuf->pos, b);
            tbuf->speed[tbuf->pos >> 3] = speed;
            if (++tbuf->pos >= tbuf->len)
                tbuf->pos = 0;
        }
    } else {
        unsigned int shift = (type == TB_all);
        if (type == TB_even)
            x >>= 1;
        for (i = 0; i < (8 << shift); i++) {
            uint8_t b = !!((x << ((i|1) >> shift)) & 0x80); /* data bit */
            if (!(i & 1)) /* clock bit */
                b = (!tbuf->prev_data_bit && !b) << 7;
            else
                tbuf->prev_data_bit = b;
            change_bit(tbuf->mfm, tbuf->pos, b);
            tbuf->speed[tbuf->pos >> 3] = speed;
            if (++tbuf->pos >= tbuf->len)
                tbuf->pos = 0;
        }
    }
}

void tbuf_bytes(struct track_buffer *tbuf, uint16_t speed,
                enum tbuf_data_type type, unsigned int bytes, void *data)
{
    unsigned int i;

    if (type == TB_even_odd) {
        tbuf_bytes(tbuf, speed, TB_even, bytes, data);
        type = TB_odd;
    } else if (type == TB_odd_even) {
        tbuf_bytes(tbuf, speed, TB_odd, bytes, data);
        type = TB_even;
    }

    for (i = 0; i < bytes; i++)
        tbuf_bits(tbuf, speed, type, 8, ((unsigned char *)data)[i]);
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
