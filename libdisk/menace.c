/*
 * disk/menace.c
 * 
 * Custom format as used on Menace by Psygnosis.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x552a,0x2a55 :: Sync
 *  u16 dat[0xc1c][2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data
 * 
 * TRKTYP_menace data layout:
 *  u8 sector_data[6200]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *menace_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[0xc1d], csum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x552a2a55)
            continue;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(MFM_even_odd, 2, raw, &dat[i]);
            csum += ntohs(dat[i]);
        }

        csum -= ntohs(dat[0xc1c]);
        if (csum != ntohs(dat[0xc1c]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void menace_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x552a2a55);

    for (i = csum = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 16, ntohs(dat[i]));
        csum += ntohs(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 16, csum);
}

struct track_handler menace_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_mfm = menace_write_mfm,
    .read_mfm = menace_read_mfm
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
