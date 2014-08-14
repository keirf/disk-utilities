/*
 * disk/dugger.c
 * 
 * Custom format as used on Dugger by Linel.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u32 dat_bytes[2]  :: Odd/even
 *  u32 header[2]     :: Odd/even
 *  u32 dat[dat_bytes/4][2] :: Odd/even
 *  u32 csum[2]       :: AmigaDOS style
 * 
 * TRKTYP_dugger data layout:
 *  u8 sector_data[dat_bytes]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *dugger_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[7012/4];
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[0]);
        if ((ti->len = be32toh(dat[0])) > 7000)
            continue;

        for (i = 1; i < ti->len/4+3; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
        }

        if ((be32toh(dat[1]) != (0x03e90100 | tracknr)) ||
            (amigados_checksum(dat, i*4) != 0))
            continue;

        ti->bytes_per_sector = ti->len;
        block = memalloc(ti->len);
        memcpy(block, &dat[2], ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void dugger_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[7012/4];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    dat[0] = htobe32(ti->len);
    dat[1] = htobe32(0x03e90100 | tracknr);
    memcpy(&dat[2], ti->dat, ti->len);
    for (i = 0; i < ti->len/4+2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32,
              amigados_checksum(dat, i*4));
}

struct track_handler dugger_handler = {
    .nr_sectors = 1,
    .write_raw = dugger_write_raw,
    .read_raw = dugger_read_raw
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
