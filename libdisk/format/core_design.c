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
#include "../private.h"

static void *core_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t mfm[2], csum, *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0x8915)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, 4, mfm, mfm);
        csum = be32toh(mfm[0]);

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
                goto fail;
            mfm_decode_bytes(MFM_even_odd, 4, mfm, &block[i]);
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

static void core_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8915);

    for (i = 0; i < ti->len/4; i++)
        csum += be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, be32toh(dat[i]));
}

struct track_handler core_design_handler = {
    .bytes_per_sector = 11*512,
    .nr_sectors = 1,
    .write_mfm = core_write_mfm,
    .read_mfm = core_read_mfm
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
