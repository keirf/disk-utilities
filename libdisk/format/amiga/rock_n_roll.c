/*
 * disk/rock_n_roll.c
 *
 * Custom format as used on Rock 'n Roll from Rainbow Arts
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x5242, 0x5284, 0x5484 :: Sync
 *  u32 dat[ti->len/4]
 *  u32 checksum
 *
 * Checksum is the decoded data eor'd
 * 
 * There are 3 different syncs and no pattern to 
 * to the use of the syncs
 * 
 * TRKTYP_rock_n_roll data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct rock_n_roll_info {
    uint16_t sync;
};

static void *rock_n_roll_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct rock_n_roll_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[2*ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        for (i = sum = 0; i < ti->len/4; i++) {
            sum ^= be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            goto fail;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void rock_n_roll_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct rock_n_roll_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

    for (i = sum = 0; i < ti->len/4; i++) {
        sum ^= be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler rock_n_roll_a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = rock_n_roll_write_raw,
    .read_raw = rock_n_roll_read_raw,
    .extra_data = & (struct rock_n_roll_info) {
        .sync = 0x5242
    }
};

struct track_handler rock_n_roll_b_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = rock_n_roll_write_raw,
    .read_raw = rock_n_roll_read_raw,
    .extra_data = & (struct rock_n_roll_info) {
        .sync = 0x5284
    }
};

struct track_handler rock_n_roll_c_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = rock_n_roll_write_raw,
    .read_raw = rock_n_roll_read_raw,
    .extra_data = & (struct rock_n_roll_info) {
        .sync = 0x5484
    }
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
