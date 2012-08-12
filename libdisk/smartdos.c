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
#include "private.h"

#include <arpa/inet.h>

static void *smartdos_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[ti->len/2], csum, sum, *block;
        unsigned int i;

        if ((uint16_t)s->word != 0x4488)
            continue;
        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bytes(s, dat, 8) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, 4, dat, dat);
        csum = ntohl(dat[0]);

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;

        for (i = sum = 0; i < 3102; i++) {
            uint32_t n = sum + ntohl(dat[i]);
            sum = (n < sum) ? n + 1 : n;
        }

        sum = sum ^ ((sum << 8) & 0xf00u) ^ ((sum >> 24) & 0xf0u);
        sum &= 0x0ffffff0u;

        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(MFM_even_odd, 4, &dat[2*i], &block[i]);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void mfm_encode_even_odd(
    uint32_t w_prev, uint32_t w, uint32_t *p_e, uint32_t *p_o)
{
    uint32_t e = 0, o = 0;
    unsigned int i;

    for (i = 0; i < 16; i++) {
        e = (e << 1) | ((w >> 31) & 1);
        o = (o << 1) | ((w >> 30) & 1);
        w <<= 2;
    }

    *p_e = mfm_encode_word((w_prev << 16) | e);
    *p_o = mfm_encode_word((e << 16) | o);
}


static void smartdos_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum = 0, prev = 0, e, o, n;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4488);

    for (i = 0; i < 1551; i++) {
        mfm_encode_even_odd(prev, ntohl(dat[i]), &e, &o);
        n = sum + e;
        sum = (n < sum) ? n + 1 : n;
        n = sum + o;
        sum = (n < sum) ? n + 1 : n;
        prev = ntohl(dat[i]);
    }

    sum = sum ^ ((sum << 8) & 0xf00u) ^ ((sum >> 24) & 0xf0u);
    sum &= 0x0ffffff0u;

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
}

struct track_handler smartdos_handler = {
    .bytes_per_sector = 6204+12,
    .nr_sectors = 1,
    .write_mfm = smartdos_write_mfm,
    .read_mfm = smartdos_read_mfm
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
