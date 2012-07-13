/*
 * disk/federation_of_free_traders.c
 * 
 * Custom format as used in Federation Of Free Traders by Gremlin.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  3 back-to-back sectors with explicit sector gap.
 *  Total encoded sector size, including gap, is 0xfc8 (4040) bytes.
 * RAW SECTOR:
 *  u8 0xa1,0xa1   :: 0x4489 sync marks
 *  u8 0xff
 *  u8 trk^1,sec
 *  u8 data[2000]
 *  u16 csum
 *  u8 gap[13]
 * MFM encoding:
 *  No even/odd split
 * 
 * TRKTYP_federation_of_free_traders data layout:
 *  u8 sector_data[5][1024]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *federation_of_free_traders_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->len);
    unsigned int i, valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        uint32_t idx_off = s->index_offset - 31;
        uint16_t csum;
        uint8_t sec, *p;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (mfm_decode_bits(MFM_all, s->word) != (0xff00 | (tracknr^1)))
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto done;
        sec = mfm_decode_bits(MFM_all, (uint16_t)s->word);
        if ((sec >= ti->nr_sectors) || (valid_blocks & (1u<<sec)))
            continue;

        p = &block[sec * ti->bytes_per_sector];
        for (i = csum = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto done;
            csum ^= (uint16_t)s->word;
            p[i] = mfm_decode_bits(MFM_all, (uint16_t)s->word);
        }

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (csum != mfm_decode_bits(MFM_all, s->word))
            continue;

        valid_blocks |= 1u << sec;
        if (!(valid_blocks & ((1u<<sec)-1)))
            ti->data_bitoff = idx_off;
    }

done:
    if (valid_blocks == 0) {
        free(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;

    for (i = 0; i < ti->nr_sectors; i++)
        if (valid_blocks & (1u << i))
            break;
    ti->data_bitoff -= i * 0xfc8;

    return block;
}

/*
 * Checksum is over encoded MFM words, *including* clock bits. We manufacture
 * the appropriate clock bits here.
 */
static uint32_t mfm_encode_word(uint16_t w)
{
    uint32_t i, d, p = 0, x = 0;
    for (i = 0; i < 16; i++) {
        d = !!(w & 0x8000u);
        x = (x << 2) | (!(d|p) << 1) | d;
        p = d;
        w <<= 1;
    }
    return x;
}

static void federation_of_free_traders_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        uint16_t csum = 0, w;
        /* header */
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, tracknr^1);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, i);
        /* data */
        w = i; /* preceding data byte, so first clock bit is correct */
        for (j = 0; j < ti->bytes_per_sector; j++) {
            w = (w << 8) | dat[j];
            csum ^= (uint16_t)mfm_encode_word(w);
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, dat[j]);
        }
        /* csum */
        if (!(ti->valid_sectors & (1u << i)))
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, csum);
        /* gap */
        for (j = 0; j < 13; j++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
        dat += ti->bytes_per_sector;
    }
}

struct track_handler federation_of_free_traders_handler = {
    .bytes_per_sector = 2000,
    .nr_sectors = 3,
    .write_mfm = federation_of_free_traders_write_mfm,
    .read_mfm = federation_of_free_traders_read_mfm
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
