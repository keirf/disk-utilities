/*
 * disk/outzone.c
 * 
 * Custom format for Outzone from Lankhor
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u16 0x5554
 *  u32 data[5640/4]
 * 
 * Checksum is part of the decoded data
 *  Checksum is 0 - sum of all decoded longs.
 * 
 * TRKTYP_outzone data layout:
 *  u8 sector_data[5640]
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *outzone_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*ti->len/4], dat[ti->len/4], i, csum;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;

        if ((uint16_t)s->word != 0x5554)
            continue;

        for (i = csum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void outzone_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5554);
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler outzone_handler = {
    .bytes_per_sector = 5640,
    .nr_sectors = 1,
    .write_raw = outzone_write_raw,
    .read_raw = outzone_read_raw
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
