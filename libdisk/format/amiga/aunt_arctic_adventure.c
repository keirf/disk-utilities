/*
 * disk/aunt_arctic_adventure.c
 *
 * Custom format as used on Aunt Arctic Adventure by Mindware International.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0xa425, 0xa425 Sync
 *  u32 0x55545554 Padding
 *  u32 0x54484242 ('THBB')
 *  u32 track length 0x1700
 *  u32 Checksum
 *  u32 dat[6000/4]
 *
 * Checksum eor'd over decoded data
 *
 * TRKTYP_aunt_arctic_adventure data layout:
 *  u8 sector_data[6000]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SIG_THBB 0x54484242

static void *aunt_arctic_adventure_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], raw2[2*ti->len/4], dat[ti->len/4], sig, len, csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0xa425a425)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55545554)
            continue;

        /* signature */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);
        if (be32toh(sig) != SIG_THBB)
            continue;

        /* track length 0x1770 */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &len);
        if (be32toh(len) != ti->len)
            continue;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* data */
        if (stream_next_bytes(s, raw2, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw2, dat);

        /* calculate checksum */
        for (i = sum = 0; i < ti->len/4; i++)
            sum ^= be32toh(dat[i]);

        if (be32toh(csum) != sum)
            continue;

        ti->total_bits = 102500;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void aunt_arctic_adventure_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa425a425);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55545554);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, SIG_THBB);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, ti->len);

    for (i = sum = 0; i < ti->len/4; i++)
        sum ^= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler aunt_arctic_adventure_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = aunt_arctic_adventure_write_raw,
    .read_raw = aunt_arctic_adventure_read_raw
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
