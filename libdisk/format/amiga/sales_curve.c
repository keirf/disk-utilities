/*
 * disk/sales_curve.c
 *
 * Custom format as used on Swiv and Saint Dragon by Sales Curve.
 *
 * Written in 2019 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489  :: Sync
 *  u16 0x5555  :: Sync
 *  u32 dat[‭6240‬/4]
 *  u32 checksum
 *
 * TRKTYP_sales_Curve data layout:
 *  u8 sector_data[‭6240‬]
 *
 * Custom format as used on Ninja Warriors by Sales Curve.
 *
 * Written in 2019 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x448A  :: Sync
 *  u16 0xAAAA :: Sync
 *  u32 dat[‭6240‬/4]
 *  u32 checksum
 *
 * TRKTYP_sales_Curve data layout:
 *  u8 sector_data[‭6240‬]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sales_curve_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4], csum, sum;
        uint16_t sync;
        unsigned int i;
        char *block;

        if (ti->type == TRKTYP_ninja_warriors)
            sync = 0x448A;
        else
            sync = 0x4489;

        if ((uint16_t)s->word != sync)
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->total_bits = 105400;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}


static void sales_curve_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    uint16_t sync, word2;
    unsigned int i;

    if (ti->type == TRKTYP_ninja_warriors) {
        sync = 0x448A;
        word2 = 0xAAAA;
    }
    else {
        sync = 0x4489;
        word2 = 0x5555;
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, word2);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, csum);
    
}

struct track_handler sales_curve_handler = {
    .bytes_per_sector = 6240,
    .nr_sectors = 1,
    .write_raw = sales_curve_write_raw,
    .read_raw = sales_curve_read_raw
};

struct track_handler ninja_warriors_handler = {
    .bytes_per_sector = 6240,
    .nr_sectors = 1,
    .write_raw = sales_curve_write_raw,
    .read_raw = sales_curve_read_raw
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
