/*
 * disk/viaje_al_centro_de_la_tierra.c
 * 
 * Custom format as used on Viaje Al Centro De La Tierra by Topo Soft.
 * 
 * I am a lamer --- this decoder is in essence a reimplementation of Psygore's
 * WHDLoad imager. Thanks Psygore. :)
 * 
 * Written in 2014 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489
 * 12 sectors back-to-back: (all u32 values are MFM-encoded even-then-odd)
 *  u32 0xfafafafa,0,0
 *  u32 data[128]
 *  u32 csum            :: EOR.L over all MFM data bits
 *  u32 0
 *  u16 sync            :: Raw value from syncs[] array below
 * 
 * TRKTYP_viaje data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include "../private.h"

static const uint16_t syncs[] = {
    0x548a, 0x5225, 0x5489, 0x5522, 0x5229, 0x4a8a,
    0x52a2, 0x522a, 0x5224, 0x448a };

static void *viaje_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int nr_valid_blocks = 0, sec;

retry:
    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset - 31;

        for (sec = 0; sec < ti->nr_sectors; sec++) {

            uint32_t csum, i, dat[2], secdat[128];

            if (stream_next_bytes(s, dat, 8) == -1)
                goto out;
            mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
            if (be32toh(dat[0]) != 0xfafafafa)
                goto retry;

            for (i = 0; i < 2; i++) {
                if (stream_next_bytes(s, dat, 8) == -1)
                    goto out;
                mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
                if (be32toh(dat[0]) != 0)
                    goto retry;
            }

            for (i = csum = 0; i < 128; i++) {
                if (stream_next_bytes(s, dat, 8) == -1)
                    goto out;
                csum ^= be32toh(dat[0]) ^ be32toh(dat[1]);
                mfm_decode_bytes(bc_mfm_even_odd, 4, dat, &secdat[i]);
            }

            csum &= 0x55555555;
            if (stream_next_bytes(s, dat, 8) == -1)
                goto out;
            mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
            if ((csum == be32toh(dat[0])) && !is_valid_sector(ti, sec)) {
                memcpy(&block[sec*512], secdat, 512);
                set_sector_valid(ti, sec);
                nr_valid_blocks++;
            }

            if (stream_next_bytes(s, dat, 8) == -1)
                goto out;
            mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
            if (be32toh(dat[0]) != 0)
                goto retry;

            if (stream_next_bits(s, 32) == -1)
                goto out;
            if (((s->word >> 16) != (uint16_t)s->word)
                || ((uint16_t)s->word != syncs[sec]))
                goto retry;
        }
    }

out:
    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    ti->total_bits = 102500;
    return block;
}

static void viaje_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i, sec;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0xfafafafa);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
        for (i = csum = 0; i < 128; i++) {
            uint32_t x = be32toh(*dat++);
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, x);
            csum ^= x;
        }
        csum ^= csum >> 1;
        csum &= 0x55555555;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[sec]);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[sec]);
    }
}

struct track_handler viaje_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = viaje_write_raw,
    .read_raw = viaje_read_raw
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
