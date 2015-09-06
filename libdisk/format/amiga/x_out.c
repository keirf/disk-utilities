/*
 * disk/x_out.c
 * 
 * Custom format as used on X-Out by Rainbow Arts.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8455 :: Sync
 *  u8  0
 *  u32 csum   :: odd bits only
 *  u32 data[0x5d4] :: even/odd
 * 
 * Checksum is sum of all MFM data bits, AND 0x55555555.
 * 
 * TRKTYP_x_out data layout:
 *  u8 sector_data[5968]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *x_out_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        uint32_t sum, csum, dat[(2*ti->len)/4];

        if (s->word != 0x84552aaa)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        csum = s->word & 0x55555555u;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;

        sum = 0;
        for (i = 0; i < ARRAY_SIZE(dat); i++)
            sum += be32toh(dat[i]) & 0x55555555u;
        sum &= 0x55555555u;
        if (sum != csum)
            continue;

        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, block);

        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void x_out_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t sum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8455);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    sum = 0;
    for (i = 0; i < ti->len/4; i++) {
        uint32_t x = be32toh(dat[i]);
        sum += x & 0x55555555u;
        sum += (x>>1) & 0x55555555u;
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd, 32, sum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler x_out_handler = {
    .bytes_per_sector = 5968,
    .nr_sectors = 1,
    .write_raw = x_out_write_raw,
    .read_raw = x_out_read_raw
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
