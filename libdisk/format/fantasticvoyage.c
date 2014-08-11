/*
 * disk/fantastic_voyage.c
 *
 * Custom format as used on Fantastic Voyage by Centaur.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489  :: Sync
 *  u16 0
 *  u8 :: track number
 *  u8 :: data checksum
 *  u16 0x4d48
 *  u32 checksum
 *  u32 dat[6144/4]
 *
 * TRKTYP_fantastic_voyage data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *fantastic_voyage_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4], csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44892aaa)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if ((uint16_t)be32toh(csum) != 0x4d48)
            continue;

        if (tracknr != be32toh(csum) >> 24)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->total_bits = 105400;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static uint32_t track_byte_checksum(
    void *dat, unsigned int tracknr,  unsigned int bytes)
{
    uint8_t d2, d4;
    uint16_t d3;
    uint32_t *p = dat;
    unsigned int i, j;
    d3 = 0x17ff;

    for (i = d2 = 0; i < bytes; i++){
        for (j = 0; j < 4; j++){
            d4 = (uint8_t)(p[i] >> (j*8));
            d4 += (uint8_t)d3;
            d2 ^= d4;
            d3 --;
        }
    }
    return (tracknr << 24) + (d2 << 16) + 0x4d48;
}

static void fantastic_voyage_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, chk, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44892aaa);

    chk = track_byte_checksum(dat, tracknr, ti->len/4);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, chk);

    for (i = csum = 0; i < ti->len/4; i++)
        csum += be32toh(dat[i]);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler fantastic_voyage_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = fantastic_voyage_write_raw,
    .read_raw = fantastic_voyage_read_raw
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
