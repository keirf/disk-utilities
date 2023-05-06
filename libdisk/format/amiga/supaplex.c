/*
 * disk/supaplex.c
 *
 * Custom format as used on Supaplex by Dream.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16  0x4489 :: sync
 *  u32  0x55555149 :: padding
 *  u16  0x452A :: padding
 *  u32 dat[6152/4]
 * 
 * The checksum is in the 2nd to last u32 of the data and the checksum
 * calculation is the sum of all decoded u32.  
 * 
 * TRKTYP_supaplex data layout:
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
        sum += be32toh(dat[i]);
    }
    return (uint32_t)sum;
}

static void *supaplex_write_raw(
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

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555149)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x452A)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, ti->len, dat, dat);


        /* checksum calulation */
        sum = checksum(&dat[0],ti->len/4-2);
        if (sum != be32toh(dat[ti->len/4-2]))
            continue;

        ti->total_bits = 100500;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void supaplex_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555149);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x452A);

    /* checksum */
    sum = checksum(&dat[0],ti->len/4-2);
    dat[ti->len/4-2] = be32toh(sum);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
}

struct track_handler supaplex_handler = {
    .bytes_per_sector = 6152,
    .nr_sectors = 1,
    .write_raw = supaplex_write_raw,
    .read_raw = supaplex_read_raw
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