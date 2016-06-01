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
 * Checksum is over encoded MFM words, *including* clock bits.
 * 
 * TRKTYP_federation_of_free_traders data layout:
 *  u8 sector_data[5][1024]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *federation_of_free_traders_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->len);
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t idx_off = s->index_offset_bc - 31;
        uint16_t csum;
        uint8_t sec, *p;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (mfm_decode_word(s->word) != (0xff00 | (tracknr^1)))
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto done;
        sec = mfm_decode_word((uint16_t)s->word);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        p = &block[sec * ti->bytes_per_sector];
        for (i = csum = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto done;
            csum ^= (uint16_t)s->word;
            p[i] = mfm_decode_word((uint16_t)s->word);
        }

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (csum != mfm_decode_word(s->word))
            continue;

        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        if (least_block > sec) {
            ti->data_bitoff = idx_off;
            least_block = sec;
        }
    }

done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    for (i = 0; i < ti->nr_sectors; i++)
        if (is_valid_sector(ti, i))
            break;
    ti->data_bitoff -= i * 0xfc8;

    return block;
}

static void federation_of_free_traders_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        uint16_t csum = 0, w;
        /* header */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr^1);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, i);
        /* data */
        w = i; /* preceding data byte, so first clock bit is correct */
        for (j = 0; j < ti->bytes_per_sector; j++) {
            w = (w << 8) | dat[j];
            csum ^= (uint16_t)mfm_encode_word(w);
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[j]);
        }
        /* csum */
        if (!is_valid_sector(ti, i))
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, csum);
        /* gap */
        for (j = 0; j < 13; j++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        dat += ti->bytes_per_sector;
    }
}

struct track_handler federation_of_free_traders_handler = {
    .bytes_per_sector = 2000,
    .nr_sectors = 3,
    .write_raw = federation_of_free_traders_write_raw,
    .read_raw = federation_of_free_traders_read_raw
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
