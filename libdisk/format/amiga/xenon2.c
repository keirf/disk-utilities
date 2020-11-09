/*
 * xenon2.c
 *
 * Custom longtrack format as used on The Power Pack 1-disk version of
 * Xenon 2 / Bitmap Brothers.
 *
 * Written in 2020 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u16 0xa1a1
 *  u32 data[2][1728+1][2] :: bc_mfm_odd_even, last long is ADD.L checksum
 * TRKTYP_xenon2 data layout:
 *  u8 sector_data[6912]
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *xenon2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t sum, dat[ti->len/4+1], raw[ti->len/2+2];
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < (ti->type == TRKTYP_xenon2 ? 2 : 1); i++) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (mfm_decode_word(s->word) != 0xa1a1)
                break;
        }
        if (mfm_decode_word(s->word) != 0xa1a1)
            continue;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat), raw, dat);

        for (i = sum = 0; i < ti->len/4; i++)
            sum += be32toh(dat[i]);
        if (sum != be32toh(dat[i]))
            continue;

        stream_next_index(s);
        ti->total_bits = (ti->type == TRKTYP_xenon2 ? 100500 : 111600);

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void xenon2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t sum, dat[ti->len/4+1];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4489);
    for (i = 0; i < (ti->type == TRKTYP_xenon2 ? 2 : 1); i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xa1a1);

    memcpy(dat, ti->dat, ti->len);
    for (i = sum = 0; i < ti->len/4; i++)
        sum += be32toh(dat[i]);
    dat[i] = htobe32(sum);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len+4, dat);
}


struct track_handler xenon2_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = xenon2_write_raw,
    .read_raw = xenon2_read_raw
};

struct track_handler xenon2_longtrack_handler = {
    .bytes_per_sector = 6912,
    .nr_sectors = 1,
    .write_raw = xenon2_write_raw,
    .read_raw = xenon2_read_raw
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
