/*
 * disk/spherical.c
 *
 * Custom format as used on Spherical & Conqueror by Rainbow Arts.
 *
 * Written in 2012 by Keir Fraser
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aaa :: Sync for Spherical, Conqueror
 *  u16 0x4445,0x2aaa :: Sync for Conqueror
 *  u32 dat[0x500][2] :: Interleaved even/odd
 *  u32 csum[2] :: Even/odd, ADD.L sum over data
 *
 * TRKTYP_rainbow_arts data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rainbow_arts_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x501], csum, sync;
        unsigned int i;
        char *block;

        sync = (ti->type == TRKTYP_spherical) ? 0x44892aaa
            : 0x44452aaa;

        if (s->word != sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        csum -= 2*be32toh(dat[i-1]);
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101200;
        return block;
    }

fail:
    return NULL;
}

static void rainbow_arts_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    uint16_t sync;
    unsigned int i;

    sync = (ti->type == TRKTYP_spherical) ? 0x4489
        : 0x4445;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler spherical_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
};

struct track_handler conqueror_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
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
