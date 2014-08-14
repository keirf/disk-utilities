/*
 * disk/crackdown.c
 *
 * Custom format as used on Crackdown by Sega/US Gold.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u16 track[2] :: track number
 *  u16 dat[0xc00][2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, eor.w sum over data
 *
 * TRKTYP_crackdown data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *crackdown_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[0xc00], csum, sum, trk;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &trk);
        if (be16toh(trk) != (uint16_t)tracknr)
            continue;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum ^= be16toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);

        /* Fix bug in track 152.  The data is good but the checksum
         * is off by 1.  Let the check pass and the correct checksum
         * will be generated when written.
         */
        csum= be16toh(csum);
        if (tracknr == 152 && csum == 0x6628)
            csum += 1;

        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void crackdown_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)tracknr);

    for (i = csum = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        csum ^= be16toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
}

struct track_handler crackdown_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = crackdown_write_raw,
    .read_raw = crackdown_read_raw
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
