/*
 * disk/typhoon_thompson.c
 *
 * Custom format as used on Typhoon Thompson by Brøderbund.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4891 :: Sync
 *  u32 0x489144a9 :: Sync
 *  u32 csum  :: Even/odd words, eor.w over raw data
 *  u32 track :: track number
 *  u32 dat[6144/4]
 *
 * TRKTYP_typhoon_thompson data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *typhoon_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t csum, sum, hdr, raw2[2], raw[2*ti->len/4], dat[(ti->len)/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4891)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x489144a9)
            continue;

        ti->data_bitoff = s->index_offset - 31;

        if (stream_next_bytes(s, raw2, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw2, &csum);

        if (stream_next_bytes(s, raw2, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw2, &hdr);

        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

        sum = be32toh(raw2[0]) ^ be32toh(raw2[1]);
        for(i = 0; i < 2*ti->len/4; i++){
             sum ^= be32toh(raw[i]);
        }
        sum &= 0x55555555;

        if (sum != be32toh(csum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
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

    csum ^= mfm_encode_word((w_prev << 16) | e);
    csum ^= mfm_encode_word((e << 16) | o);
    return csum;
}

static void typhoon_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint16_t *)ti->dat, prev, csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4891);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x489144a9);

    prev = 0x4891; /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        csum ^= csum_long(prev, be32toh(dat[i]));
        prev = be32toh(dat[i]);
    }
    csum ^= csum_long(prev, tracknr);
    csum &= 0x55555555;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, tracknr);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

}

struct track_handler typhoon_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = typhoon_write_raw,
    .read_raw = typhoon_read_raw
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
