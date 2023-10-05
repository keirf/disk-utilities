/*
 * disk/jeff_spangenberg.c
 * 
 * Custom formats used in the "Sega Arcade Smash Hits" collection, including:
 *  After Burner (Sega / Weebee Games)
 *  Out Run (Sega / US Gold)
 *  Thunder Blade (Sega / US Gold / Tiertex)
 * 
 * Written in 2012 by Keir Fraser
 * 
 * Hackmatt Disk Protection & Hackmat v2.0 Disk Loader 
 * developed by Jeff Spangenberg for Sega game releases
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* TRKTYP_hackmat:
 *  u16 0xa245 :: Sync
 *  u32 0x55555555
 *  u32 0xaaaaaaaa
 *  u32 csum[2]      :: Even/odd longs, SUB.L sum of all decoded data longs
 *  u32 dat[1500][2] :: Even/odd longs
 * TRKTYP_outrun_sega: 0x4489 sync
 * TRKTYP_thunderblade_sega: 0x4891 sync
 * 
 * Data layout:
 *  u8 data[6000]
 *  u8 nr_sync_marks
 * 
 */

struct hackmat_2_info {
    uint16_t sync;
};

static void *hackmat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hackmat_2_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1501], csum;
        unsigned int i, nr_sync = 1;
        char *block;

        /* Check for sync mark */
        if ((uint16_t)s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* Check for optional second sync mark */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word == info->sync) {
            nr_sync++;
            if (stream_next_bits(s, 16) == -1)
                goto fail;
        }

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        if (csum != 0)
            continue;

        block = memalloc(ti->len+1);
        memcpy(block, &dat[1], ti->len);
        set_all_sectors_valid(ti);
        block[ti->len] = nr_sync;
        ti->len++;
        return block;
    }

fail:
    return NULL;
}

static void hackmat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hackmat_2_info *info = handlers[ti->type]->extra_data;
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i, nr_sync = ti->dat[ti->len-1];

    for (i = 0; i < nr_sync; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    for (i = csum = 0; i < ti->len/4; i++)
        csum -= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler sega_boot_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = hackmat_write_raw,
    .read_raw = hackmat_read_raw,
    .extra_data = & (struct hackmat_2_info) {
        .sync = 0xa245
    }
};

struct track_handler outrun_sega_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = hackmat_write_raw,
    .read_raw = hackmat_read_raw,
    .extra_data = & (struct hackmat_2_info) {
        .sync = 0x4489
    }
};

struct track_handler thunderblade_sega_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = hackmat_write_raw,
    .read_raw = hackmat_read_raw,
    .extra_data = & (struct hackmat_2_info) {
        .sync = 0x4891
    }
};

/* TRKTYP_hackmatt_v2:
 *
 * The format supports both the US version of Afterburner and Altered
 * Beast.
 *  
 * Afterburner
 *  u16 0xa245a245 :: Sync
 *  u32 hdr[2]
 *  u32 dat[1550][2] :: Even/odd longs
 *  u32 csum[2]
 * 
 * Altered Beast
 *  u16 0x48544854 :: Sync
 *  u32 hdr[2]
 *  u32 dat[1550][2] :: Even/odd longs
 *  u32 csum[2]
 * 
 * Checksum is over encoded MFM longs, *including* clock bits.
 * Header contains cyl#, plus an unpredictable second word, hence we include
 * the header in the output data. 
 * 
 * Hackmat V2.0 Disk Protection developed by Jeff Spangenberg 
 */


struct hackmat_v2_info {
    uint32_t sync;
};

static void *hackmat_v2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hackmat_v2_info *info = handlers[ti->type]->extra_data;
    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1551], csum, sum;
        unsigned int i;
        char *block;

        if (s->word != info->sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum -= be32toh(raw[0]) + be32toh(raw[1]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
        if (csum != be32toh(sum))
            continue;

        if (((be32toh(dat[0]) >> 16) != (tracknr/2)) ||
            (((uint16_t)be32toh(dat[0]) != 0x0001) &&
             ((uint16_t)be32toh(dat[0]) != 0xff01)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void hackmat_v2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hackmat_v2_info *info = handlers[ti->type]->extra_data;
    uint32_t csum, *dat = (uint32_t *)ti->dat, raw[2];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sync);

    raw[1] = htobe32(info->sync); /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[i], raw, be32toh(raw[1]));
        csum -= be32toh(raw[0]) + be32toh(raw[1]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler afterburner_sega_handler = {
    .bytes_per_sector = 6204,
    .nr_sectors = 1,
    .write_raw = hackmat_v2_write_raw,
    .read_raw = hackmat_v2_read_raw,
    .extra_data = & (struct hackmat_v2_info) {
        .sync = 0xa245a245
    }
};

struct track_handler altered_beast_sega_handler = {
    .bytes_per_sector = 6204,
    .nr_sectors = 1,
    .write_raw = hackmat_v2_write_raw,
    .read_raw = hackmat_v2_read_raw,
    .extra_data = & (struct hackmat_v2_info) {
        .sync = 0x48544854
    }
};

/* Space Harrier (Sega)
 *
 * This format it similar to the sega decoder, but has 2 different syncs 
 * 0xa245 and 0x4489
 *
 * TRKTYP_space_harrier_sega:
 *  u16 0xa245, 0x4489 :: Sync
 *  u32 0x55555555
 *  u32 0xaaaaaaaa
 *  u32 csum[2]      :: Even/odd longs, SUB.L sum of all decoded data longs
 *  u32 dat[1500][2] :: Even/odd longs
 * 
 * Data layout:
 *  u8 data[6000]
 * */

static void *space_harrier_sega_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1501], csum;
        unsigned int i;
        char *block;

        /* first sync */
        if ((uint16_t)s->word != 0xa245)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* second sync */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4489)
            continue;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        /* data and checksum calculation */
        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat[1], ti->len);
        set_all_sectors_valid(ti);
        ti->len++;
        return block;
    }

fail:
    return NULL;
}

static void space_harrier_sega_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa245);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    /* cheksum */
    for (i = csum = 0; i < ti->len/4; i++)
        csum -= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    /* data */
    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler space_harrier_sega_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = space_harrier_sega_write_raw,
    .read_raw = space_harrier_sega_read_raw
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
