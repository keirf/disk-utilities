/*
 * disk/pinball_prelude.c
 *
 * Custom format as used on Pinball Prelude by Effigy
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x44A2, 0x4522, 0x5122, 0x2244 :: Sync
 *  u8 dat[6324]
 * 
 * There are 4 different syncs used in a repeating order
 * 
 *  Tracks      Sync
 *       2      0x44A2
 *       3      0x44A2
 *       4      0x4522
 *       5      0x4522
 *       6      0x5122
 *       7      0x5122
 *       8      0x2244
 *       9      0x2244
 *      repeat sequence above
 * 
 * The checksum is part of the data:
 *   dat[4] << 24 | dat[5] << 16 | dat[6] << 8 | dat[7];
 * 
 * The checksum the sum of the decoded data starting from 
 * offset 12 and rotated left after each long.
 *
 *
 * TRKTYP_pinball_prelude data layout:
 *  u8 sector_data[6324]
 * 
 */


#include <libdisk/util.h>
#include <private/disk.h>

struct pinball_prelude_info {
    uint16_t sync;
};

const static uint16_t syncs[] = {
    0x44A2, 0x4522, 0x5122, 0x2244
};

uint32_t rol(uint32_t n, uint32_t value){
  return (value << n) | (value >> (32-n));
}

static void *pinball_prelude_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    while (stream_next_bit(s) != -1) {

        uint8_t raw[2], dat[ti->len];
        unsigned int i;
        uint32_t sum, csum;
        char *block;

        /* sync */
        if ((uint16_t)s->word != syncs[((tracknr-2)/2)%4])
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* data */
        for (i = 0; i < ti->len; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm, 1, raw, &dat[i]);
        }

        /* get checksum */
        csum = dat[4] << 24 | dat[5] << 16 | dat[6] << 8 | dat[7];

        /* calculate checksum */
        sum = 0;
        for (i = 0xc; i < ti->len; i+=4) {
            sum += dat[i] << 24 | dat[i+1] << 16 | dat[i+2] << 8 | dat[i+3];
            sum = rol(3, sum);
        }

        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 106000;
        return block;
    }

fail:
    return NULL;
}

static void pinball_prelude_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint32_t sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[((tracknr-2)/2)%4]);

    /* calculate checksum */
    sum = 0;
    for (i = 0xc; i < ti->len; i+=4) {
        sum += dat[i] << 24 | dat[i+1] << 16 | dat[i+2] << 8 | dat[i+3];
        sum = rol(3, sum);
    }

    dat[4] = (uint8_t)(sum >> 24);
    dat[5] = (uint8_t)(sum >> 16);
    dat[6] = (uint8_t)(sum >> 8);
    dat[7] = (uint8_t)sum;

    /* data */
    for (i = 0; i < ti->len; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
    }
}

struct track_handler pinball_prelude_handler = {
    .bytes_per_sector = 6324,
    .nr_sectors = 1,
    .write_raw = pinball_prelude_write_raw,
    .read_raw = pinball_prelude_read_raw
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
