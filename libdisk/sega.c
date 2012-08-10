/*
 * disk/sega.c
 * 
 * Custom formats as used on:
 *  After Burner (Sega / Activision)
 *  Out Run (Sega / US Gold)
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

/*
 * TRKTYP_afterburner_boot:
 *  u16 0xa245 :: Sync
 *  u32 0x55555555
 *  u32 0xaaaaaaaa
 *  u32 csum[2]      :: Even/odd longs, SUB.L sum of all decoded data longs
 *  u32 dat[1500][2] :: Even/odd longs
 * 
 * TRKTYP_outrun:
 *  u16 0x4489,0x4489 :: Sync
 *  ...as afterburner_boot...
 */

static void *afterburner_boot_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1501], csum;
        unsigned int i;
        char *block;

        if (ti->type == TRKTYP_afterburner_boot) {
            if ((uint16_t)s->word != 0xa245)
                continue;
            ti->data_bitoff = s->index_offset - 15;
        } else /* TRKTYP_outrun */ {
            if (s->word != 0x44894489)
                continue;
            ti->data_bitoff = s->index_offset - 31;
        }

        if (stream_next_bits(s, 32) == -1)
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
            mfm_decode_bytes(MFM_even_odd, 4, raw, &dat[i]);
            csum += ntohl(dat[i]);
        }

        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat[1], ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        return block;
    }

fail:
    return NULL;
}

static void afterburner_boot_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    if (ti->type == TRKTYP_afterburner_boot) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0xa245);
    } else /* TRKTYP_outrun */ {
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
    }

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x55555555);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0xaaaaaaaa);

    for (i = csum = 0; i < ti->len/4; i++)
        csum -= ntohl(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
}

struct track_handler afterburner_boot_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_mfm = afterburner_boot_write_mfm,
    .read_mfm = afterburner_boot_read_mfm
};

struct track_handler outrun_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_mfm = afterburner_boot_write_mfm,
    .read_mfm = afterburner_boot_read_mfm
};

/*
 * TRKTYP_afterburner:
 *  u16 0xa245a245 :: Sync
 *  u32 hdr[2]
 *  u32 dat[1550][2] :: Even/odd longs
 *  u32 csum[2]
 * Checksum is over encoded MFM longs, *including* clock bits.
 * Header contains cyl#, plus an unpredictable second word, hence we include
 * the header in the output data.
 */

static void *afterburner_write_mfm(
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
            mfm_decode_bytes(MFM_even_odd, 4, raw, &dat[i]);
            csum -= ntohl(raw[0]) + ntohl(raw[1]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, 4, raw, &sum);
        if (csum != ntohl(sum))
            continue;

        if (((ntohl(dat[0]) >> 16) != (tracknr/2)) ||
            (((uint16_t)ntohl(dat[0]) != 0x0001) &&
             ((uint16_t)ntohl(dat[0]) != 0xff01)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
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

static void afterburner_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat, prev;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0xa245a245);

    prev = 0xa245a245; /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
        csum += csum_long(prev, ntohl(dat[i]));
        prev = ntohl(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);
}

struct track_handler afterburner_handler = {
    .bytes_per_sector = 6204,
    .nr_sectors = 1,
    .write_mfm = afterburner_write_mfm,
    .read_mfm = afterburner_read_mfm
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
