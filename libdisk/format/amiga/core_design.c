/*
 * disk/core_design.c
 * 
 * Custom format as used by various releases by Core Design:
 *   Jaguar XJ220
 *   Premiere
 *   Thunderhawk AH-73M
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8915 :: Sync
 *  u32 checksum
 *  u32 data[11*512/4]
 *  Checksum is sum of all decoded longs
 * MFM encoding:
 *  Alternating even/odd longs
 * 
 * TRKTYP_core_design data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *core_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], csum;
        unsigned int i;

        if ((uint16_t)s->word != 0x8915)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
        csum = be32toh(csum);

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &block[i]);
            csum -= be32toh(block[i]);
        }

        if (csum)
            continue;

        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void core_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8915);

    for (i = 0; i < ti->len/4; i++)
        csum += be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler core_design_handler = {
    .bytes_per_sector = 11*512,
    .nr_sectors = 1,
    .write_raw = core_write_raw,
    .read_raw = core_read_raw
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
