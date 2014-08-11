/*
 * disk/sega.c
 * 
 * Custom formats used in the "Sega Arcade Smash Hits" collection, including:
 *  After Burner (Sega / Weebee Games)
 *  Out Run (Sega / US Gold)
 *  Thunder Blade (Sega / US Gold / Tiertex)
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* TRKTYP_sega_boot:
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
 *  u8 nr_sync_marks */

static uint16_t sega_sync(uint16_t type)
{
    switch (type) {
    case TRKTYP_sega_boot: return 0xa245;
    case TRKTYP_outrun_sega: return 0x4489;
    case TRKTYP_thunderblade_sega: return 0x4891;
    }
    BUG();
}

static void *sega_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t sync = sega_sync(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1501], csum;
        unsigned int i, nr_sync = 1;
        char *block;

        /* Check for sync mark */
        if ((uint16_t)s->word != sync)
            continue;
        ti->data_bitoff = s->index_offset - 15;

        /* Check for optional second sync mark */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word == sync) {
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

static void sega_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i, nr_sync = ti->dat[ti->len-1];

    for (i = 0; i < nr_sync; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sega_sync(ti->type));

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
    .write_raw = sega_write_raw,
    .read_raw = sega_read_raw
};

struct track_handler outrun_sega_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = sega_write_raw,
    .read_raw = sega_read_raw
};

struct track_handler thunderblade_sega_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = sega_write_raw,
    .read_raw = sega_read_raw
};

/* TRKTYP_afterburner_sega:
 *  u16 0xa245a245 :: Sync
 *  u32 hdr[2]
 *  u32 dat[1550][2] :: Even/odd longs
 *  u32 csum[2]
 * Checksum is over encoded MFM longs, *including* clock bits.
 * Header contains cyl#, plus an unpredictable second word, hence we include
 * the header in the output data. */

static void *afterburner_sega_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1551], csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0xa245a245)
            continue;

        ti->data_bitoff = s->index_offset - 31;

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

static uint32_t csum_long(uint32_t w_prev, uint32_t w)
{
    uint32_t e = 0, o = 0, csum = 0;
    unsigned int i;

    for (i = 0; i < 16; i++) {
        e = (e << 1) | ((w >> 31) & 1);
        o = (o << 1) | ((w >> 30) & 1);
        w <<= 2;
    }

    csum -= mfm_encode_word((w_prev << 16) | e);
    csum -= mfm_encode_word((e << 16) | o);
    return csum;
}

static void afterburner_sega_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat, prev;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa245a245);

    prev = 0xa245a245; /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += csum_long(prev, be32toh(dat[i]));
        prev = be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler afterburner_sega_handler = {
    .bytes_per_sector = 6204,
    .nr_sectors = 1,
    .write_raw = afterburner_sega_write_raw,
    .read_raw = afterburner_sega_read_raw
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
