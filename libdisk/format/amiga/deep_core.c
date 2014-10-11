/*
 * disk/deep_core.c
 * 
 * Custom format as used in Deep Core by ICE Ltd.
 * 
 * Written in 2014 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 <sync>
 *  u32 checksum :: EOR.l over decoded data (even/odd encoded)
 *  u32 data[] :: Even/odd encoded
 */

#include <libdisk/util.h>
#include <private/disk.h>

static bool_t block_write_raw(
    struct stream *s, void *_dat, unsigned int bytes)
{
    uint32_t csum, raw[2], *dat = _dat;
    unsigned int i;

    if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
        goto fail;
    mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

    for (i = 0; i < bytes/4; i++) {
        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        csum ^= dat[i];
    }

    return !csum;

fail:
    return 0;
}

static void block_read_raw(
    struct tbuf *tbuf, void *_dat, unsigned int bytes)
{
    uint32_t csum, *dat = _dat;
    unsigned int i;

    for (i = csum = 0; i < bytes/4; i++)
        csum ^= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < bytes/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

static void *sec_1_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s, uint16_t sync)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block;

    if (!sync)
        return NULL;

    ti->nr_sectors = 1;
    ti->bytes_per_sector = 6552;
    ti->len = ti->nr_sectors * ti->bytes_per_sector;
    block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (!block_write_raw(s, block, ti->len))
            continue;

        ti->total_bits = 105500;
        set_all_sectors_valid(ti);
        return block;
    }

    memfree(block);
    return NULL;
}

static void sec_1_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf, uint16_t sync)
{
    struct track_info *ti = &d->di->track[tracknr];

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);

    block_read_raw(tbuf, ti->dat, ti->len);
}

static void *sec_13_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s,
    const uint16_t *syncs)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block;
    unsigned int sec;

    ti->nr_sectors = 13;
    ti->bytes_per_sector = 496;
    ti->len = ti->nr_sectors * ti->bytes_per_sector;
    block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != syncs[0])
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (sec = 0; sec < ti->nr_sectors; sec++) {
            if ((uint16_t)s->word != syncs[sec])
                break;
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (mfm_decode_bits(bc_mfm, (uint16_t)s->word) != 0)
                break;
            if (!block_write_raw(s, &block[sec*ti->bytes_per_sector],
                                 ti->bytes_per_sector))
                continue;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (mfm_decode_bits(bc_mfm, s->word) != 0)
                break;
            if (stream_next_bits(s, 16) == -1)
                goto fail;
        }
        if (sec != ti->nr_sectors)
            continue;

        ti->total_bits = 105500;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    memfree(block);
    return NULL;
}

static void sec_13_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf,
    const uint16_t *syncs)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int sec;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[sec]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        block_read_raw(tbuf, &ti->dat[sec*ti->bytes_per_sector],
                       ti->bytes_per_sector);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    }
}

/* DISK 1 */

static uint16_t disk1_sync(unsigned int tracknr)
{
    if (tracknr >= 2 && tracknr <= 3)
        return 0x448a;
    if (tracknr == 4)
        return 0x4222;
    if (tracknr >= 5 && tracknr <= 78)
        return 0x4215;
    if (tracknr >= 79 && tracknr <= 127)
        return 0x4221;
    return 0;
}

static void *deep_core_disk1_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    return sec_1_write_raw(d, tracknr, s, disk1_sync(tracknr));
}

static void deep_core_disk1_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    sec_1_read_raw(d, tracknr, tbuf, disk1_sync(tracknr));
}

struct track_handler deep_core_disk1_handler = {
    .write_raw = deep_core_disk1_write_raw,
    .read_raw = deep_core_disk1_read_raw
};


/* DISK 2 */

static uint16_t disk2_sync(unsigned int tracknr)
{
    if (tracknr == 0)
        return 0x422a;
    if (tracknr >= 2 && tracknr <= 109)
        return 0x4242;
    return 0;
}

static void *deep_core_disk2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    return sec_1_write_raw(d, tracknr, s, disk2_sync(tracknr));
}

static void deep_core_disk2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    sec_1_read_raw(d, tracknr, tbuf, disk2_sync(tracknr));
}

struct track_handler deep_core_disk2_handler = {
    .write_raw = deep_core_disk2_write_raw,
    .read_raw = deep_core_disk2_read_raw
};


/* DISK 3 */

static const uint16_t d3_t2_syncs[] = {
    0x4211, 0x4212, 0x4215, 0x4221, 0x4222, 0x4225, 0x4229,
    0x422a, 0x4242, 0x4245, 0x4249, 0x424a, 0x4251
};
static const uint16_t d3_t3_syncs[] = {
    0x4252, 0x4255, 0x4285, 0x4289, 0x428a, 0x4291, 0x4292,
    0x4295, 0x42a1, 0x42a2, 0x42a5, 0x42a9, 0x4421
};
static const uint16_t d3_t4_syncs[] = {
    0x4422, 0x4425, 0x4429, 0x4442, 0x4485, 0x4489, 0x448a,
    0x44a1, 0x44a2, 0x4509, 0x450a, 0x4521, 0x4522
};

static void *deep_core_disk3_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    if (tracknr == 2)
        return sec_13_write_raw(d, tracknr, s, d3_t2_syncs);
    if (tracknr == 3)
        return sec_13_write_raw(d, tracknr, s, d3_t3_syncs);
    if (tracknr == 4)
        return sec_13_write_raw(d, tracknr, s, d3_t4_syncs);
    return NULL;
}

static void deep_core_disk3_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    if (tracknr == 2)
        sec_13_read_raw(d, tracknr, tbuf, d3_t2_syncs);
    if (tracknr == 3)
        sec_13_read_raw(d, tracknr, tbuf, d3_t3_syncs);
    if (tracknr == 4)
        sec_13_read_raw(d, tracknr, tbuf, d3_t4_syncs);
}

struct track_handler deep_core_disk3_handler = {
    .write_raw = deep_core_disk3_write_raw,
    .read_raw = deep_core_disk3_read_raw
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
