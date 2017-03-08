/*
 * disk/silkworm.c
 *
 * Custom format as used by Silkworm.
 *
 *
 * Written in 2017 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489
 *  u32 0x55555555
 *  u32 dat[5632/4]
 *  u32 checksum
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *silkworm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;
        
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            continue;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (sum != be32toh(csum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}


static void silkworm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler silkworm_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = silkworm_write_raw,
    .read_raw = silkworm_read_raw
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
