/*
 * disk/killing_game_show_demo.c
 *
 * Custom format as used on Awesome Demo by Psygnosis.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * 
 * TRKTYP_killing_gameshow_demo_a data layout:
 *  u8 sector_data[334+4]
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: sync
 *  u16 0x5555 :: padding
 *  u16 dat[ti->len/2]
 * 
 * The checksum is in the first word of the data and the checksum
 * calculation is the sum words via addx. The last word of data is
 * not counted in the checksum.
 * 
 * 
 * TRKTYP_killing_gameshow_demo_b data layout:
 *  u8 sector_data[6296+4]
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: sync
 *  u16 0x5555 :: padding
 *  u32 dat[ti->len/4]
 * 
 * The checksum is in the first word of the data and the checksum
 * calculation is the sum words via addx. The last word of data is
 * not counted in the checksum.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>


static uint16_t checksum(uint16_t *dat, unsigned int nr)
{
    unsigned int i;
    uint32_t sum = -2;

    for (i = 0; i < nr; i++) {
        /* Simulate M68K ADDX instruction */
        if (sum > 0xffff)
            sum = (uint16_t)(sum+1);
        sum += be16toh(dat[i]);
    }
    return (uint16_t)sum;
}

static void *killing_gameshow_demo_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[ti->len/2], sum;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
             break;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* data */
        for (i = 0; i < ti->len/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
        }
 
        /* checksum validation */
        sum = checksum(&dat[1],ti->len/2-2);
        if (sum != be16toh(dat[0]))
            continue;

        ti->total_bits = 106000;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void killing_gameshow_demo_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);

    /* checksum */
    sum = checksum(&dat[1],ti->len/2-2);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, sum);

    /* data */
    for (i = 1; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
    }
}

struct track_handler killing_gameshow_demo_a_handler = {
    .bytes_per_sector = 334+4,
    .nr_sectors = 1,
    .write_raw = killing_gameshow_demo_a_write_raw,
    .read_raw = killing_gameshow_demo_a_read_raw
};


static void *killing_gameshow_demo_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        uint16_t cdat[ti->len/2-2], sum;
        unsigned int i, j;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
             break;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* data */
        for (i = j = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            if (i > 0) {
                cdat[j] = be16toh((uint16_t)(be32toh(dat[i]) >> 16));
                j++;
            }
            if (i < ti->len/4-1) {
                cdat[j] = be16toh((uint16_t)be32toh(dat[i]));
                j++;
            }
        }

        /* checksum validation */
        sum = checksum(cdat,ti->len/2-2);
        if (sum != (be32toh(dat[0]) >> 16))
            continue;

        ti->total_bits = 106000;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void killing_gameshow_demo_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    uint16_t *cdat = (uint16_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);

    /* checksum */
    sum = checksum(&cdat[1],ti->len/2-2);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (sum << 16) | (uint16_t)be32toh(dat[0]));

    /* data */
    for (i = 1; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler killing_gameshow_demo_b_handler = {
    .bytes_per_sector = 6296+4,
    .nr_sectors = 1,
    .write_raw = killing_gameshow_demo_b_write_raw,
    .read_raw = killing_gameshow_demo_b_read_raw
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
