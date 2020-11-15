/*
 * libdisk/disk.c
 * 
 * Framework for container types and track-format handlers.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

static void tbuf_finalise(struct tbuf *tbuf);

static struct container *container_from_filename(
    const char *name)
{
    char suffix[8];

    filename_extension(name, suffix, sizeof(suffix));

    if (!strcmp(suffix, "adf"))
        return &container_adf;
    if (!strcmp(suffix, "eadf"))
        return &container_eadf;
    if (!strcmp(suffix, "dsk"))
        return &container_dsk;
    if (!strcmp(suffix, "hfe"))
        return &container_hfe;
    if (!strcmp(suffix, "imd"))
        return &container_imd;
    if (!strcmp(suffix, "img") || !strcmp(suffix, "st"))
        return &container_img;
    if (!strcmp(suffix, "ipf"))
        return &container_ipf;
    if (!strcmp(suffix, "scp"))
        return &container_scp;
    if (!strcmp(suffix, "jv3"))
        return &container_jv3;

    warnx("Unknown file suffix: %s", name);
    return NULL;
}

struct disk *disk_create(const char *name, unsigned int flags)
{
    struct disk *d;
    struct container *c;
    int fd;
    unsigned int rpm = flags >> DISKFL_rpm_shift;

    if ((c = container_from_filename(name)) == NULL)
        return NULL;

    if ((fd = file_open(name, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1) {
        warn("%s", name);
        return NULL;
    }

    d = memalloc(sizeof(*d));
    d->fd = fd;
    d->read_only = 0;
    d->kryoflux_hack = !!(flags & DISKFL_kryoflux_hack);
    d->rpm = rpm ?: DEFAULT_RPM;
    d->container = c;

    c->init(d);

    return d;
}

struct disk *disk_open(const char *name, unsigned int flags)
{
    struct disk *d;
    struct container *c;
    int fd, read_only = !!(flags & DISKFL_read_only);
    unsigned int rpm = flags >> DISKFL_rpm_shift;

    if ((c = container_from_filename(name)) == NULL)
        return NULL;

    if ((fd = file_open(name, read_only ? O_RDONLY : O_RDWR)) == -1) {
        warn("%s", name);
        return NULL;
    }

    d = memalloc(sizeof(*d));
    d->fd = fd;
    d->read_only = read_only;
    d->kryoflux_hack = !!(flags & DISKFL_kryoflux_hack);
    d->rpm = rpm ?: DEFAULT_RPM;
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

struct track_raw *track_alloc_raw_buffer(struct disk *d)
{
    struct tbuf *tbuf = memalloc(sizeof(*tbuf));
    tbuf->disk = d;
    tbuf->prng_seed = TBUF_PRNG_INIT;
    return &tbuf->raw;
}

void track_free_raw_buffer(struct track_raw *track_raw)
{
    struct tbuf *tbuf = container_of(track_raw, struct tbuf, raw);

    track_purge_raw_buffer(track_raw);
    memfree(tbuf);
}

void track_purge_raw_buffer(struct track_raw *track_raw)
{
    memfree(track_raw->bits);
    memfree(track_raw->speed);
    memset(track_raw, 0, sizeof(*track_raw));
}

void track_read_raw(struct track_raw *track_raw, unsigned int tracknr)
{
    struct tbuf *tbuf = container_of(track_raw, struct tbuf, raw);
    struct disk *d = tbuf->disk;
    struct disk_info *di = d->di;
    struct track_info *ti;
    const struct track_handler *thnd;

    track_purge_raw_buffer(track_raw);

    if (tracknr >= di->nr_tracks)
        return;
    ti = &di->track[tracknr];

    if ((int32_t)ti->total_bits > 0)
        tbuf_init(tbuf, ti->data_bitoff, ti->total_bits);

    thnd = handlers[ti->type];
    thnd->read_raw(d, tracknr, tbuf);

    tbuf_finalise(tbuf);
}

int track_write_raw(
    struct track_raw *raw, unsigned int tracknr, enum track_type type,
    unsigned int rpm)
{
    struct tbuf *tbuf = container_of(raw, struct tbuf, raw);
    struct stream *s = stream_soft_open(
        raw->bits, raw->speed, raw->bitlen, rpm);
    int rc = track_write_raw_from_stream(tbuf->disk, tracknr, type, s);
    stream_close(s);
    return rc;
}

int track_write_raw_from_stream(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    memfree(ti->dat);
    ti->dat = NULL;

    return d->container->write_raw(d, tracknr, type, s);
}

struct sbuf {
    struct track_sectors sectors;
    struct disk *disk;
};

struct track_sectors *track_alloc_sector_buffer(struct disk *d)
{
    struct sbuf *sbuf = memalloc(sizeof(*sbuf));
    sbuf->disk = d;
    return &sbuf->sectors;
}

void track_free_sector_buffer(struct track_sectors *track_sectors)
{
    struct sbuf *sbuf = container_of(track_sectors, struct sbuf, sectors);

    track_purge_sector_buffer(track_sectors);
    memfree(sbuf);
}

void track_purge_sector_buffer(struct track_sectors *track_sectors)
{
    memfree(track_sectors->data);
    memset(track_sectors, 0, sizeof(*track_sectors));
}

int track_read_sectors(
    struct track_sectors *track_sectors, unsigned int tracknr)
{
    struct sbuf *sbuf = container_of(track_sectors, struct sbuf, sectors);
    struct disk *d = sbuf->disk;
    struct disk_info *di = d->di;
    struct track_info *ti;
    const struct track_handler *thnd;

    track_purge_sector_buffer(track_sectors);

    if (tracknr >= di->nr_tracks)
        return -1;
    ti = &di->track[tracknr];

    thnd = handlers[ti->type];
    if (thnd->read_sectors == NULL)
        return -1;

    thnd->read_sectors(d, tracknr, track_sectors);
    return track_sectors->data ? 0 : -1;
}

int track_write_sectors(
    struct track_sectors *track_sectors, unsigned int tracknr,
    enum track_type type)
{
    struct sbuf *sbuf = container_of(track_sectors, struct sbuf, sectors);
    struct disk *d = sbuf->disk;
    struct disk_info *di = d->di;
    struct track_info *ti;
    const struct track_handler *thnd;
    unsigned int ns_per_cell = 0;

    if (tracknr >= di->nr_tracks)
        return -1;
    ti = &di->track[tracknr];

    memfree(ti->dat);
    memset(ti, 0, sizeof(*ti));
    init_track_info(ti, type);

    thnd = handlers[ti->type];
    if (thnd->write_sectors == NULL)
        goto fail;

    switch (thnd->density) {
    case trkden_single: ns_per_cell = 4000u; break;
    case trkden_double: ns_per_cell = 2000u; break;
    case trkden_high: ns_per_cell = 1000u; break;
    case trkden_extra: ns_per_cell = 500u; break;
    default: BUG();
    }
    ti->total_bits = (DEFAULT_BITS_PER_TRACK(d) * 2000u) / ns_per_cell;

    ti->dat = thnd->write_sectors(d, tracknr, track_sectors);
    if (ti->dat == NULL)
        goto fail;

    return 0;

fail:
    track_mark_unformatted(d, tracknr);
    ti->typename = "Unformatted*";
    return -1;
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

struct disktag *disk_get_tag_by_id(struct disk *d, uint16_t id)
{
    struct disk_list_tag *dltag;
    for (dltag = d->tags; dltag != NULL; dltag = dltag->next)
        if (dltag->tag.id == id)
            return &dltag->tag;
    return NULL;
}

struct disktag *disk_get_tag_by_idx(struct disk *d, unsigned int idx)
{
    struct disk_list_tag *dltag;
    unsigned int i;
    for (dltag = d->tags, i = 0;
         (dltag != NULL) && (i < idx);
         dltag = dltag->next, i++)
        continue;
    return dltag ? &dltag->tag : NULL;
}

struct disktag *disk_set_tag(
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

void track_get_format_name(
    struct disk *d, unsigned int tracknr, char *str, size_t size)
{
    struct disk_info *di = d->di;
    struct track_info *ti;
    const struct track_handler *thnd;

    if (tracknr >= di->nr_tracks) {
        snprintf(str, size, "???");
        return;
    }

    ti = &di->track[tracknr];
    thnd = handlers[ti->type];

    if (thnd->get_name)
        thnd->get_name(d, tracknr, str, size);
    else
        snprintf(str, size, "%s", ti->typename ?: "???");
}

int is_valid_sector(struct track_info *ti, unsigned int sector)
{
    BUG_ON(sector >= ti->nr_sectors);
    return (ti->valid_sectors[sector>>3] >> (~sector&7)) & 1;
}

void set_sector_valid(struct track_info *ti, unsigned int sector)
{
    BUG_ON(sector >= ti->nr_sectors);
    ti->valid_sectors[sector>>3] |= 1u << (~sector&7);
}

void set_sector_invalid(struct track_info *ti, unsigned int sector)
{
    BUG_ON(sector >= ti->nr_sectors);
    ti->valid_sectors[sector>>3] &= ~(1u << (~sector&7));
}

void set_all_sectors_valid(struct track_info *ti)
{
    unsigned int sector;
    set_all_sectors_invalid(ti);
    for (sector = 0; sector < ti->nr_sectors; sector++)
        set_sector_valid(ti, sector);
}

void set_all_sectors_invalid(struct track_info *ti)
{
    memset(ti->valid_sectors, 0, sizeof(ti->valid_sectors));
}


/* PRIVATE HELPERS */

void init_track_info(struct track_info *ti, enum track_type type)
{
    const struct track_handler *thnd = handlers[type];
    ti->type = type;
    ti->typename = track_format_names[type].desc_name;
    ti->bytes_per_sector = thnd->bytes_per_sector;
    ti->nr_sectors = thnd->nr_sectors;
    BUG_ON(ti->nr_sectors >= sizeof(ti->valid_sectors)*8);
    ti->len = ti->bytes_per_sector * ti->nr_sectors;
}

static void change_bit(uint8_t *map, unsigned int bit, bool_t on)
{
    if (on)
        map[bit>>3] |= 0x80 >> (bit & 7);
    else
        map[bit>>3] &= ~(0x80 >> (bit & 7));
}

static void append_bit(struct tbuf *tbuf, uint16_t speed, uint8_t x)
{
    change_bit(tbuf->raw.bits, tbuf->pos, x);
    tbuf->raw.speed[tbuf->pos] = speed;
    if (++tbuf->pos >= tbuf->raw.bitlen)
        tbuf->pos = 0;
}

static void tbuf_bit(
    struct tbuf *tbuf, uint16_t speed,
    enum bitcell_encoding enc, uint8_t dat)
{
    if (enc == bc_mfm) {
        /* Clock bit */
        uint8_t clk = !(tbuf->prev_data_bit | dat);
        append_bit(tbuf, speed, clk);
    }

    /* Data bit */
    append_bit(tbuf, speed, dat);
    tbuf->prev_data_bit = dat;
}

void tbuf_init(struct tbuf *tbuf, uint32_t bitstart, uint32_t bitlen)
{
    tbuf->start = tbuf->pos = bitstart;
    tbuf->prev_data_bit = 0;
    tbuf->gap_fill_byte = 0;
    tbuf->crc16_ccitt = 0;
    tbuf->disable_auto_sector_split = 0;
    tbuf->bit = tbuf_bit;
    tbuf->gap = NULL;
    tbuf->weak = NULL;

    memset(&tbuf->raw, 0, sizeof(tbuf->raw));
    tbuf->raw.bitlen = bitlen;
    tbuf->raw.bits = memalloc(bitlen+7/8);
    tbuf->raw.speed = memalloc(2*bitlen);
}

static uint32_t fix_bc(struct tbuf *tbuf, int32_t bc)
{
    if (bc < 0)
        bc += tbuf->raw.bitlen;
    return bc;
}

static void tbuf_finalise(struct tbuf *tbuf)
{
    int32_t pos, nr_bits;
    uint8_t b = 0;

    tbuf->raw.data_start_bc = tbuf->start;
    tbuf->raw.data_end_bc = fix_bc(tbuf, tbuf->pos - 1);

    if (tbuf->start == tbuf->pos) {
        /* Handler completely filled the buffer. */
        tbuf->raw.write_splice_bc = tbuf->raw.data_end_bc;
        return;
    }

    /* Forward fill half the gap */
    nr_bits = fix_bc(tbuf, tbuf->start - tbuf->pos);
    nr_bits /= 4; /* /2 to halve the gap, /2 to count data bits only */
    while (nr_bits--)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 1, 0);

    /* Write splice. Write an MFM-illegal string of zeroes. */
    nr_bits = fix_bc(tbuf, tbuf->start - tbuf->pos);
    nr_bits = min(nr_bits, 5); /* up to 5 bits */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, nr_bits, 0);
    tbuf->raw.write_splice_bc = fix_bc(tbuf, tbuf->pos - 1 - nr_bits/2);

    /* Reverse fill the remainder */
    for (pos = tbuf->start; pos != tbuf->pos; ) {
        if (--pos < 0)
            pos += tbuf->raw.bitlen;
        change_bit(tbuf->raw.bits, pos, b);
        tbuf->raw.speed[pos] = SPEED_AVG;
        b = !b;
    }
}

void tbuf_bits(struct tbuf *tbuf, uint16_t speed,
               enum bitcell_encoding enc, unsigned int bits, uint32_t x)
{
    int i;

    if (enc == bc_mfm_even_odd) {
        tbuf_bits(tbuf, speed, bc_mfm_even, bits, x);
        enc = bc_mfm_odd;
    } else if (enc == bc_mfm_odd_even) {
        tbuf_bits(tbuf, speed, bc_mfm_odd, bits, x);
        enc = bc_mfm_even;
    }

    if ((enc == bc_mfm_even) || (enc == bc_mfm_odd)) {
        uint32_t y = 0;
        if (enc == bc_mfm_even)
            x >>= 1;
        bits >>= 1;
        for (i = 0; i < bits; i++)
            y |= (x >> i) & (1u << i);
        x = y;
        enc = bc_mfm;
    }

    for (i = bits-1; i >= 0; i--) {
        uint8_t b = (x >> i) & 1;
        if ((enc != bc_raw) || !(i & 1))
            tbuf->crc16_ccitt = crc16_ccitt_bit(b, tbuf->crc16_ccitt);
        tbuf->bit(tbuf, speed, enc, b);
    }
}

void tbuf_bytes(struct tbuf *tbuf, uint16_t speed,
                enum bitcell_encoding enc, unsigned int bytes, void *data)
{
    unsigned int i;
    uint8_t *p;

    if (enc == bc_mfm_even_odd) {
        tbuf_bytes(tbuf, speed, bc_mfm_even, bytes, data);
        enc = bc_mfm_odd;
    } else if (enc == bc_mfm_odd_even) {
        tbuf_bytes(tbuf, speed, bc_mfm_odd, bytes, data);
        enc = bc_mfm_even;
    }

    p = (uint8_t *)data;
    for (i = 0; i < bytes; i++)
        tbuf_bits(tbuf, speed, enc, 8, p[i]);
}

void tbuf_gap(struct tbuf *tbuf, uint16_t speed, unsigned int bits)
{
    if (tbuf->gap != NULL) {
        tbuf->gap(tbuf, speed, bits);
    } else {
        while (bits--)
            tbuf->bit(tbuf, speed, bc_mfm, 0);
    }
}

void tbuf_gap_fill(struct tbuf *tbuf, uint16_t speed, uint8_t fill)
{
    unsigned int remain = fix_bc(tbuf, tbuf->start - tbuf->pos) / 16;
    while (remain--)
        tbuf_bits(tbuf, speed, bc_mfm, 8, fill);
    remain = fix_bc(tbuf, tbuf->start - tbuf->pos) / 2;
    while (remain--) {
        tbuf_bits(tbuf, speed, bc_mfm, 1, fill>>7);
        fill <<= 1;
    }
}

void tbuf_set_gap_fill_byte(struct tbuf *tbuf, uint8_t byte)
{
    tbuf->gap_fill_byte = byte;
}

void tbuf_weak(struct tbuf *tbuf, unsigned int bits)
{
    tbuf->raw.has_weak_bits = 1;
    if (tbuf->weak != NULL) {
        tbuf->weak(tbuf, bits);
    } else {
        while (bits--)
            tbuf->bit(tbuf, SPEED_WEAK, bc_mfm, tbuf_rnd16(tbuf) & 1);
    }
}

void tbuf_start_crc(struct tbuf *tbuf)
{
    tbuf->crc16_ccitt = 0xffff;
}

void tbuf_emit_crc16_ccitt(struct tbuf *tbuf, uint16_t speed)
{
    tbuf_bits(tbuf, speed, bc_mfm, 16, tbuf->crc16_ccitt);
}

void tbuf_disable_auto_sector_split(struct tbuf *tbuf)
{
    tbuf->disable_auto_sector_split = 1;
}

uint16_t tbuf_rnd16(struct tbuf *tbuf)
{
    return rnd16(&tbuf->prng_seed);
}

uint16_t mfm_decode_word(uint32_t w)
{
    return (((w & 0x40000000u) >> 15) | ((w & 0x10000000u) >> 14) |
            ((w & 0x04000000u) >> 13) | ((w & 0x01000000u) >> 12) |
            ((w & 0x00400000u) >> 11) | ((w & 0x00100000u) >> 10) |
            ((w & 0x00040000u) >>  9) | ((w & 0x00010000u) >>  8) |
            ((w & 0x00004000u) >>  7) | ((w & 0x00001000u) >>  6) |
            ((w & 0x00000400u) >>  5) | ((w & 0x00000100u) >>  4) |
            ((w & 0x00000040u) >>  3) | ((w & 0x00000010u) >>  2) |
            ((w & 0x00000004u) >>  1) | ((w & 0x00000001u) >>  0));
}

uint32_t mfm_encode_word(uint32_t w)
{
    uint32_t x;
    /* Place data bits in their encoded locations. */
    x = (((w & 0x8000u) << 15) | ((w & 0x4000u) << 14) |
         ((w & 0x2000u) << 13) | ((w & 0x1000u) << 12) |
         ((w & 0x0800u) << 11) | ((w & 0x0400u) << 10) |
         ((w & 0x0200u) <<  9) | ((w & 0x0100u) <<  8) |
         ((w & 0x0080u) <<  7) | ((w & 0x0040u) <<  6) |
         ((w & 0x0020u) <<  5) | ((w & 0x0010u) <<  4) |
         ((w & 0x0008u) <<  3) | ((w & 0x0004u) <<  2) |
         ((w & 0x0002u) <<  1) | ((w & 0x0001u) <<  0));
    /* Calculate the clock bits. */
    x |= ~((x>>1)|(x<<1)) & 0xaaaaaaaau;
    /* First clock bit is always 0 if preceding data bit was 1. */
    if (w & (1u<<16))
        x &= ~(1u<<31);
    return x;
}

void mfm_decode_bytes(
    enum bitcell_encoding enc, unsigned int bytes, void *in, void *out)
{
    uint8_t *in_b = in, *out_b = out;
    unsigned int i;

    for (i = 0; i < bytes; i++) {
        if (enc == bc_mfm) {
            uint8_t x = in_b[2*i+0], y = in_b[2*i+1];
            out_b[i] = (((x & 0x40) << 1) | ((x & 0x10) << 2) |
                        ((x & 0x04) << 3) | ((x & 0x01) << 4) |
                        ((y & 0x40) >> 3) | ((y & 0x10) >> 2) |
                        ((y & 0x04) >> 1) | ((y & 0x01) >> 0));
        } else if (enc == bc_mfm_even_odd) {
            out_b[i] = ((in_b[i] & 0x55) << 1) | (in_b[i + bytes] & 0x55);
        } else if (enc == bc_mfm_odd_even) {
            out_b[i] = (in_b[i] & 0x55) | ((in_b[i + bytes] & 0x55) << 1);
        } else {
            BUG();
        }
    }
}

void mfm_encode_bytes(
    enum bitcell_encoding enc, unsigned int bytes, void *in, void *out,
    uint8_t prev_bit)
{
    uint16_t x;
    uint8_t *in_b = in, *out_b = out;
    unsigned int i;

    /* Extract the data bits into correct output locations. */
    for (i = 0; i < bytes; i++) {
        x = in_b[i];
        if (enc == bc_mfm) {
            out_b[2*i+0] = (((x & 0x80) >> 1) | ((x & 0x40) >> 2) |
                            ((x & 0x20) >> 3) | ((x & 0x10) >> 4));
            out_b[2*i+1] = (((x & 0x08) << 3) | ((x & 0x04) << 2) |
                            ((x & 0x02) << 1) | ((x & 0x01) << 0));
        } else if (enc == bc_mfm_even_odd) {
            out_b[i] = x >> 1;
            out_b[i + bytes] = x;
        } else if (enc == bc_mfm_odd_even) {
            out_b[i] = x;
            out_b[i + bytes] = x >> 1;
        } else {
            BUG();
        }
    }

    /* Calculate and insert the clock bits. */
    x = prev_bit;
    for (i = 0; i < 2*bytes; i++) {
        x = (x << 8) | out_b[i];
        x &= 0x5555u;
        x |= ~((x>>1)|(x<<1)) & 0xaaaa;
        out_b[i] = x;
    }
}

uint32_t amigados_checksum(void *dat, unsigned int bytes)
{
    uint32_t *p = dat, csum = 0;
    unsigned int i;
    for (i = 0; i < bytes/4; i++)
        csum ^= be32toh(p[i]);
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
