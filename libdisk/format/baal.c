/*
 * disk/baal.c
 *
 * Custom format as used on Baal by Psygnosis.
 *
 * Written in 2013 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x292a,0xaaa4,0x4a49,0x4944 :: Sync
 *  u32 checksum
 *  u32 dat[6200/4]
 *
 * TRKTYP_baal data layout:
 *  u8 sector_data[6200]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *baal_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x60f], csum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;


        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x292aaaa4)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4a494944)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        csum -= be32toh(dat[0x60e]);
       // if (csum != be32toh(dat[0x60e]))
      //      continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void baal_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x292aaaa4);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4a494944);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler baal_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = baal_write_raw,
    .read_raw = baal_read_raw
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
