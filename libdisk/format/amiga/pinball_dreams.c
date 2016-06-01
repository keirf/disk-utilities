/*
 * disk/pinball_dreams.c
 * 
 * Custom format as used on Pinball Dreams by Digital Illusions.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x448a,0x448a :: Sync
 *  u32 checksum[2] :: Odd/even longs, EOR.L over raw mfm data
 *  u16 dat[0x1862] :: Encoded bc_mfm, swap nibbles of each byte
 *  u16 0x4489,0x4489
 * 
 * TRKTYP_pinball_dreams data layout:
 *  u8 sector_data[0x1862]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *pinball_dreams_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum[2], dat[0x1862/2];
        uint16_t *p;
        uint8_t *block;
        unsigned int i;

        if (s->word != 0x448a448a)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, csum, sizeof(csum)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, csum, csum);

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        for (i = csum[1] = 0; i < ARRAY_SIZE(dat); i++)
            csum[1] ^= be32toh(dat[i]);
        if (be32toh(csum[0]) != (csum[1] & 0x55555555u))
            continue;

        stream_next_bits(s, 32);
        if (s->word == 0x44894488) { /* Pinball Dreams, Disk 2, Track 157 */
            ti->total_bits = 101200;
        } else if (s->word == 0x44894489) { /* All other tracks */
            ti->total_bits = 105500;
        } else {
            trk_warn(ti, tracknr, "Did not find expected 44894489 "
                     "signature (saw %08x)", s->word);
            goto fail;
        }

        block = memalloc(ti->len);
        p = (uint16_t *)dat;
        for (i = 0; i < ti->len; i++) {
            uint8_t x = mfm_decode_word(be16toh(p[i]));
            block[i] = (x >> 4) | (x << 4);
        }
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void pinball_dreams_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *cdat = (uint16_t *)ti->dat;
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x448a448a);

    for (i = csum = 0; i < ti->len/2; i++)
        csum ^= be16toh(*cdat++);
    csum = ((csum >> 4) & 0x0f0fu) | ((csum << 4) & 0xf0f0u);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, csum);

    for (i = 0; i < ti->len; i++) {
        uint8_t x = dat[i];
        x = (x >> 4) | (x << 4);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, x);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32,
              (ti->total_bits == 105500) ? 0x44894489 : 0x44894488);
}

struct track_handler pinball_dreams_handler = {
    .bytes_per_sector = 0x1862,
    .nr_sectors = 1,
    .write_raw = pinball_dreams_write_raw,
    .read_raw = pinball_dreams_read_raw
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
