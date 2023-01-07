/*
 * disk/acid.c
 *
 * Custom format as used on Skidmarks by Acid.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16  0x4489 :: sync
 *  u16  0x2aaa :: padding
 *  u32 dat[6152/4]
 * 
 * The checksum is in the last u32 of the data and the checksum
 * calculation is the sum of al decoded u32 via addx.  Then this
 * value is subtracted from 0xffffffff. The last 2 u32 of data
 * are not counted in the checksum.
 * 
 * TRKTYP_acid data layout:
 *  u8 sector_data[6152]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>


static uint32_t checksum(uint32_t *dat, unsigned int nr)
{
    unsigned int i;
    uint64_t sum = 0;

    for (i = 0; i < nr; i++) {
        /* Simulate M68K ADDX instruction */
        if (sum > 0xffffffff)
            sum = (uint32_t)(sum+1);
        sum += be32toh(dat[i]);
    }
    return 0xffffffff - (uint32_t)sum;
}

static void *acid_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4], sum;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        /* checksum calulation */
        sum = checksum(&dat[0],ti->len/4-2);

        if (sum != be32toh(dat[ti->len/4-1]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void acid_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    /* checksum */
    sum = checksum(&dat[0],ti->len/4-2);
    dat[ti->len/4-1] = be32toh(sum);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler acid_handler = {
    .bytes_per_sector = 6152,
    .nr_sectors = 1,
    .write_raw = acid_write_raw,
    .read_raw = acid_read_raw
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