/*
 * disk/spell_bound.c
 *
 * Custom format as used on Spell Bound by Psyclapse/Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aa5,0x2aa4,0x4944,0x4945 :: Sync
 *  u32 checksum
 *  u32 dat[6232/4]
 *
 * TRKTYP_spell_bound data layout:
 *  u8 sector_data[6232]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *spell_bound_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x616], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x2924A92A)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44495245)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
        sum = be32toh(sum);
//printf("Sum: %x\n", sum);
        ti->data_bitoff = s->index_offset - 46;

        for (i = csum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum ^= be32toh(dat[i]);
        }
//printf("cSum: %x\n", csum);

       if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105800;
        return block;
    }

fail:
    return NULL;
}



static void spell_bound_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2924A92A);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44495245);

    for (i = csum = 0; i < ti->len/4; i++) {
        csum ^= be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,csum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler spell_bound_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = spell_bound_write_raw,
    .read_raw = spell_bound_read_raw
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
