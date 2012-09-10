/*
 * disk/turrican.c
 * 
 * Custom format as used on Turrican by Factor 5 / Rainbow Arts.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x9521 :: Sync
 *  u16 0x2aaa
 *  u32 data[1630][2] :: bc_mfm_even_odd alternating longs
 *  u32 csum[2]   :: bc_mfm_even_odd
 * TRKTYP_turrican data layout:
 *  u8 sector_data[6552]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *turrican_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t csum, dat[2*ti->len/4];
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_bits(bc_mfm, (uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        for (i = csum = 0; i < ti->len/4; i++) {
            csum ^= be32toh(dat[2*i]) ^ be32toh(dat[2*i+1]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[2*i], &dat[i]);
        }
        csum &= 0x55555555u;

        if (stream_next_bytes(s, &dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[ti->len/4], &dat[ti->len/4]);
        if (csum != be32toh(dat[ti->len/4]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 108000;
        return block;
    }

fail:
    return NULL;
}

static void turrican_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        csum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 4, &dat[i]);
    }
    csum &= 0x55555555u;
    
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler turrican_handler = {
    .bytes_per_sector = 6520,
    .nr_sectors = 1,
    .write_raw = turrican_write_raw,
    .read_raw = turrican_read_raw
};

/*
 * TRKTYP_factor5_hiscore:
 *  u16 0x4489
 *  u16 0x2aaa
 *  u32 checksum[2]  :: even/odd mfm
 *  u32 data[99][2] :: even/odd mfm
 * Checksum is EOR data mfm longwords, AND 0x55555555, EOR 0x12345678
 * 
 * Since the loader will handle a bad checksum, we tolerate this and create
 * a track containing just the 4489 sync word (avoids loader hang).
 */

static void *factor5_hiscore_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t sum, csum, dat[2];
        unsigned int i;

        if ((uint16_t)s->word != 0x4489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            continue;
        if (mfm_decode_bits(bc_mfm, (uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, dat, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
        csum = be32toh(dat[0]) ^ 0x12345678;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, dat, 8) == -1)
                break;
            sum ^= be32toh(dat[0]) ^ be32toh(dat[1]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, dat, &block[i]);
        }
        sum &= 0x55555555;
        if (sum != csum) {
            trk_warn(ti, tracknr, "No saved high-score data found. "
                     "Creating empty track.");
            ti->nr_sectors = ti->bytes_per_sector = ti->len = 0;
        } else {
            set_all_sectors_valid(ti);
        }
        return block;
    }

    memfree(block);
    return NULL;
}

static void factor5_hiscore_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    if (ti->len == 0)
        return;

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]) ^ be32toh(dat[i] >> 1);
    csum &= 0x55555555;
    csum ^= 0x12345678;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler factor5_hiscore_handler = {
    .bytes_per_sector = 396,
    .nr_sectors = 1,
    .write_raw = factor5_hiscore_write_raw,
    .read_raw = factor5_hiscore_read_raw
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
