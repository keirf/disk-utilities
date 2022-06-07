/*
 * disk/ilyad.c
 *
 * Custom format as used on Ilyad by UBI Soft.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4488 :: Sync
 *  u16 track/2
 *  u16 dat[6300/2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, eor'd and then not the result
 *
 * TRKTYP_ilyad data layout:
 *  u8 sector_data[6300]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *ilyad_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[ti->len/2], sum, csum, trk;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4488)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* track number / 2 */
        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &trk);

        if (tracknr/2 != be16toh(trk))
            continue;

        /* data */
        for (i = sum = 0; i < ti->len/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum ^= be16toh(dat[i]);
        }
        sum = ~sum;

        /* checksum */
        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);

        if (be16toh(csum) != sum)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void ilyad_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4488);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, tracknr/2);

    for (i = csum = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        csum ^= be16toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, ~csum);
}

struct track_handler ilyad_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = ilyad_write_raw,
    .read_raw = ilyad_read_raw
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
