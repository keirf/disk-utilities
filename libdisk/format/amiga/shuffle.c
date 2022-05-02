/*
 * disk/shuffle.c
 *
 * Custom format as used on Shuffle by Tale.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 0x2aaaaaaa
 *  u32 dat[6300/4]
 *
 * Checksum & over decoded data will always be 0
 *
 * TRKTYP_shuffle data layout:
 *  u8 sector_data[6300]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *shuffle_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x2aaaaaaa)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum &= be32toh(dat[i]);
        }

        if (sum != 0)
            continue;

        ti->total_bits = 102200;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void shuffle_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler shuffle_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = shuffle_write_raw,
    .read_raw = shuffle_read_raw
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
