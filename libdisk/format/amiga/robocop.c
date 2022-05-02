/*
 * disk/robocop.c
 *
 * Custom format as used on Robocop by Ocean.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 signature bc_mfm_even_odd = 0x41524230
 *  u32 dat[6224/4]
 *
 * No checksum in data
 *
 * TRKTYP_robocop data layout:
 *  u8 sector_data[6224]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *robocop_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sig;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);
        if (be32toh(sig) != 0x41524230)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        ti->total_bits = 105500;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void robocop_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0x41524230);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler robocop_handler = {
    .bytes_per_sector = 6224,
    .nr_sectors = 1,
    .write_raw = robocop_write_raw,
    .read_raw = robocop_read_raw
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
