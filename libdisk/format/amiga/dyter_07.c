/*
 * dyter_07.c
 *
 * Custom format as used on Dyter-07 by reLINE.
 *
 * Written in 2021 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u16 0x9122,0x9122
 *  u32 trk[2] :: e/o
 *  u32 csum[2] :: e/o
 *  u32 data[0x62a][2] :: Interleaved even/odd words
 * 
 * Checksum is ADDX over MFM longs with clock bits masked out.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t csum(uint32_t *dat, unsigned int len)
{
    uint32_t sum = 0, c = 0;
    unsigned int i;

    for (i = 0; i < len/4; i++) {
        uint32_t y = be32toh(dat[i]);
        uint32_t x = y >> 1;
        uint32_t nsum;
        x &= 0x55555555;
        y &= 0x55555555;
        nsum = sum + x + c;
        c = (nsum < sum); /* ADDX */
        sum = nsum;
        nsum = sum + y + c;
        c = (nsum < sum); /* ADDX */
        sum = nsum;
    }
    return sum;
}

static void *dyter_07_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x62c];
        unsigned int i;
        char *block;

        if (s->word != 0x91229122)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        /* Check the track header. */
        if (be32toh(dat[0]) != (0x47004942 | (tracknr<<16)))
            continue;

        /* Check the checksum. */
        if (csum(&dat[2], ti->len) != be32toh(dat[1])) {
            if ((tracknr == 0) && (csum(&dat[2], 6240) == be32toh(dat[1]))) {
                /* Dyter-07, Disk 2, Track 0: Short data (6240 bytes). */
                ti->bytes_per_sector = ti->len = 6240;
            } else {
                continue;
            }
        }

        block = memalloc(ti->len);
        memcpy(block, &dat[2], ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 102200;
        return block;
    }

fail:
    return NULL;
}

static void dyter_07_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x91229122);

    hdr = 0x47004942;
    hdr |= tracknr << 16;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum(dat, ti->len));

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler dyter_07_handler = {
    .bytes_per_sector = 6312,
    .nr_sectors = 1,
    .write_raw = dyter_07_write_raw,
    .read_raw = dyter_07_read_raw
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
