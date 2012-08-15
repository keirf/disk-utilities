/*
 * disk/spherical.c
 * 
 * Custom format as used on Spherical by Rainbow Arts.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aaa :: Sync
 *  u32 dat[0x500][2] :: Interleaved even/odd
 *  u32 csum[2] :: Even/odd, ADD.L sum over data
 * 
 * TRKTYP_spherical data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *spherical_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x501], csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44892aaa)
            continue;
        ti->data_bitoff = s->index_offset - 31;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(MFM_even_odd, 4, raw, &dat[i]);
            csum += ntohl(dat[i]);
        }

        csum -= 2*ntohl(dat[i-1]);
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->total_bits = 101200;
        stream_next_index(s);
        return block;
    }

fail:
    return NULL;
}

static void spherical_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
        csum += ntohl(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);
}

struct track_handler spherical_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_mfm = spherical_write_mfm,
    .read_mfm = spherical_read_mfm
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
