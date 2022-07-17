/*
 * disk/visionary_design.c
 *
 * Custom format as used by Visionary Design for the following games:
 * 
 * Dragon's Lair
 * Vortex
 * 
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489, 0x448a, or 0x44a2 :: Sync
 *  u32 0x55555555 :: Padding
 *  u16 0x5555 :: Padding
 *  u16 data_odd[0xCE8]
 *  u16 data_even[0xCE8]
 *
 *  There are 2 checksums one is eor over the decoded (0xCE4) data located
 *  at position 0xCE6 and the second is the sum of the decoded data (0xCE4)
 *  located at position 0xCE7
 * 
 * 
 * TRKTYP_visionary_design_b data layout:
 *  u8 sector_data[0x19D0]
 * 
 * TRKTYP_visionary_design_c data layout:
 *  u8 sector_data[0x19D0]
 * 
 * TRKTYP_visionary_design_d data layout:
 *  u8 sector_data[0x19D0]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct visionary_design_info {
    uint16_t sync;
};

static void *visionary_design_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct visionary_design_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {
        uint16_t dat[ti->len];
        uint16_t sum, sum2;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;
        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;

        /* Check for sig - original loader does this */
        if ((be16toh(dat[0xce8-3]) & 0x7fff) != 0x5244)
            continue;
        if ((be16toh(dat[0x19D0-3]) & 0x7fff) != 0x2924)
            continue;

        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        /* Calculate Checksums */
        for (i = sum = sum2 = 0; i < 0x19c8/2; i++){
            sum += be16toh(dat[i]);
            sum2 ^= be16toh(dat[i]);
        }

        if (sum != be16toh(dat[0xCE7]) || sum2 != be16toh(dat[0xCE6]))
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void visionary_design_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct visionary_design_info *info = handlers[ti->type]->extra_data;
    uint16_t *dat = (uint16_t *)ti->dat, sum, sum2;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);

    /* calculate checksum */
    for (i = sum = sum2 = 0; i < 0x19c8/2; i++){
        sum += be16toh(dat[i]);
        sum2 ^= be16toh(dat[i]);
    }
    dat[0xCE7] = be16toh(sum);
    dat[0xCE6] = be16toh(sum2);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
}

struct track_handler visionary_design_b_handler = {
    .bytes_per_sector = 6608,
    .nr_sectors = 1,
    .write_raw = visionary_design_write_raw,
    .read_raw = visionary_design_read_raw,
    .extra_data = & (struct visionary_design_info) {
        .sync = 0x4489
    }
};

struct track_handler visionary_design_c_handler = {
    .bytes_per_sector = 6608,
    .nr_sectors = 1,
    .write_raw = visionary_design_write_raw,
    .read_raw = visionary_design_read_raw,
    .extra_data = & (struct visionary_design_info) {
        .sync = 0x448a
    }
};

struct track_handler visionary_design_d_handler = {
    .bytes_per_sector = 6608,
    .nr_sectors = 1,
    .write_raw = visionary_design_write_raw,
    .read_raw = visionary_design_read_raw,
    .extra_data = & (struct visionary_design_info) {
        .sync = 0x44a2
    }
};

/*
 * disk/visionary_design.c
 *
 * Custom format as used by Visionary Design for Dragon's Lair
 * 
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 0x55555555 :: Padding
 *  u16 0x5555 :: Padding
 *  u16 data_odd[0xce6]
 *  u16 data_even[0xce6]
 *
 * There is no checksum for this decoder and it only used for 9 tracks on disk 1
 * 
 * TRKTYP_visionary_design_a data layout:
 *  u8 sector_data[0x19CC]
 */

static void *visionary_design_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct visionary_design_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {
        uint16_t dat[ti->len];
        char *block;

        /* sync */
        if ((uint16_t)s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;

        /* Check for sig - original loader does this */
        if ((be16toh(dat[0xce8-3]) & 0x7fff) != 0x5244)
            continue;
        if ((be16toh(dat[0x19D0-5]) & 0x7fff) != 0x2924)
            continue;

        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        if (be16toh(dat[0xCE5]) != 0x524c)
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void visionary_design_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct visionary_design_info *info = handlers[ti->type]->extra_data;
    uint16_t *dat = (uint16_t *)ti->dat;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
}

struct track_handler visionary_design_a_handler = {
    .bytes_per_sector = 6604,
    .nr_sectors = 1,
    .write_raw = visionary_design_a_write_raw,
    .read_raw = visionary_design_a_read_raw,
    .extra_data = & (struct visionary_design_info) {
        .sync = 0x4489
    }
};


/*
 * Track 0.1 on one of the versions has a longtrack with a sync of 0x4489 and
 * consits entirely of 0x5555. I did not see any access to this track, but for 
 * consistency I included it.
 */


static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

static void *vortex_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (!check_sequence(s, 3000, 0xff))
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return memalloc(0);
    }

    return NULL;
}

static void vortex_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    for (i = 0; i < 6640/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);
}

struct track_handler vortex_b_handler = {
    .write_raw = vortex_b_write_raw,
    .read_raw = vortex_b_read_raw
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
