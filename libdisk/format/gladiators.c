/*
 * disk/gladiators.c
 * 
 * Custom format as used on Gladiators by Smash 16.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8915 :: Sync
 *  u32 dat[1536][2] :: Interleaved even/odd words
 *  u32 csum[2] :: Even/odd words, ADD.L sum over raw MFM data
 * 
 * TRKTYP_gladiators data layout:
 *  u8 sector_data[6*1024]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *gladiators_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1536], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x8915)
            continue;
        ti->data_bitoff = s->index_offset - 15;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(MFM_even_odd, 4, raw, &dat[i]);
            csum += be32toh(raw[0]) + be32toh(raw[1]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, 4, raw, &sum);
        if (csum != be32toh(sum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static uint32_t csum_long(uint32_t w_prev, uint32_t w)
{
    uint32_t e = 0, o = 0, csum = 0;
    unsigned int i;

    for (i = 0; i < 16; i++) {
        e = (e << 1) | ((w >> 31) & 1);
        o = (o << 1) | ((w >> 30) & 1);
        w <<= 2;
    }

    csum += mfm_encode_word((w_prev << 16) | e);
    csum += mfm_encode_word((e << 16) | o);
    return csum;
}

static void gladiators_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat, prev;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8915);

    prev = 0x8915; /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, be32toh(dat[i]));
        csum += csum_long(prev, be32toh(dat[i]));
        prev = be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);
}

struct track_handler gladiators_handler = {
    .bytes_per_sector = 6*1024,
    .nr_sectors = 1,
    .write_mfm = gladiators_write_mfm,
    .read_mfm = gladiators_read_mfm
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
