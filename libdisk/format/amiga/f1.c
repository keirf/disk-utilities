/*
 * f1.c
 * 
 * Custom format used on F1 by Domark.
 * 
 * Written in 2020 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 4489
 *  u32 0xfe000000 + tracknr
 *  u32 dat[0x5b5] :: even/odd
 *  u32 csum
 * Encoding is alternating even/odd, per longword.
 * Checksum is ADD.L over all decoded data longs.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *f1_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[ti->len/4+2], raw[2], sum, i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }
        if ((be32toh(dat[0]) != (0xfe000000 | tracknr)) || (sum != 0))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat+1, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void f1_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum, i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4489);

    sum = 0xfe000000 | tracknr;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, -sum);
}

struct track_handler f1_handler = {
    .bytes_per_sector = 5844,
    .nr_sectors = 1,
    .write_raw = f1_write_raw,
    .read_raw = f1_read_raw
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
