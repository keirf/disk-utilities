/*
 * disk/kelloggs_land.c
 * 
 * Custom format used on promotional title Tony & Friends in Kellogg's Land
 * by Factor 5.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 4489,2aa9
 *  u32 dat[0x600][2] :: even/odd
 *  u32 csum[2]       :: even/odd
 * Checksum is ADD.L over all decoded data longs.
 * 
 * TRKTYP_kelloggs_land data layout:
 *  u8 sector_data[0x1800]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *kelloggs_land_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[0x601], raw[2], sum, i;
        char *block;

        if (s->word != 0x44892aa9)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }
        sum -= be32toh(dat[i-1]);
        if (sum != be32toh(dat[i-1]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void kelloggs_land_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum, i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44892aa9);

    for (i = sum = 0; i < 0x600; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler kelloggs_land_handler = {
    .bytes_per_sector = 0x1800,
    .nr_sectors = 1,
    .write_raw = kelloggs_land_write_raw,
    .read_raw = kelloggs_land_read_raw
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
