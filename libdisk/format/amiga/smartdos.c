/*
 * disk/smartdos.c
 * 
 * Custom format as used on Rise Of The Robots by Mirage / Time Warner.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4488        :: Sync
 *  u32 csum[2]       :: Even/odd. Based on 1s-complement sum of encoded data.
 *  u32 dat[1551][2]  :: Even/odd longs
 *  u32 extra_dat[3][2] :: Extra unchecksummed data!
 * 
 * TRKTYP_smartdos data layout:
 *  u8 sector_data[6216]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *smartdos_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[ti->len/2], csum, sum, *block;
        unsigned int i;

        if ((uint16_t)s->word != 0x4488)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, dat, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
        csum = be32toh(dat[0]);

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;

        for (i = sum = 0; i < 3102; i++) {
            uint32_t n = sum + be32toh(dat[i]);
            sum = (n < sum) ? n + 1 : n;
        }

        sum = sum ^ ((sum << 8) & 0xf00u) ^ ((sum >> 24) & 0xf0u);
        sum &= 0x0ffffff0u;

        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[2*i], &block[i]);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void smartdos_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum = 0, raw[2], n;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4488);

    /* Calculate the 1s-complement checksum. */
    raw[0] = raw[1] = 0; /* get 1st clock bit right */
    for (i = 0; i < 1551; i++) {
        mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[i], raw, be32toh(raw[1]));
        n = sum + be32toh(raw[0]);
        sum = (n < sum) ? n + 1 : n;
        n = sum + be32toh(raw[1]);
        sum = (n < sum) ? n + 1 : n;
    }

    sum = sum ^ ((sum << 8) & 0xf00u) ^ ((sum >> 24) & 0xf0u);
    sum &= 0x0ffffff0u;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler smartdos_handler = {
    .bytes_per_sector = 6204+12,
    .nr_sectors = 1,
    .write_raw = smartdos_write_raw,
    .read_raw = smartdos_read_raw
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
