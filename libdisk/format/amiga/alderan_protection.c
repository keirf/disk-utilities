/*
 * disk/alderan_protection.c
 *
 * Custom format as used on alderan: Gra Slow by Alderan.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4429 :: Sync
 *  u16 0x5552 :: padding
 *  u16 checksum :: 0x61d0
 *  u32 dat[ti->len*2]
 *
 * TODO: Implement the decoding and encoding of the
 * data. Currently passing it as raw data as they use
 * a strange decoding algorithm.
 *
 * TRKTYP_alderan_protection data layout:
 *  u8 sector_data[6294*2]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *alderan_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], csum;
        uint32_t raw32[ti->len/4+1], sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4429)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5552)
            continue;

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);

        if (be16toh(csum) != 0x61d0)
            continue;

        raw32[ti->len/4] = be16toh(csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            raw32[i] = s->word;
            sum ^= s->word;
        }

        if (sum != 0x1917bf6b)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len+4);
        memcpy(block, raw32, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void alderan_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4429);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5552);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)dat[ti->len/4]);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[i]);
    }

}

struct track_handler alderan_protection_handler = {
    .bytes_per_sector = 6294*2,
    .nr_sectors = 1,
    .write_raw = alderan_protection_write_raw,
    .read_raw = alderan_protection_read_raw
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
