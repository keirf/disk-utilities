/*
 * disk/eliminator.c
 *
 * Custom format as used on Eliminator by Hewson.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *
 * TRKTYP_eliminator_a data layout:
 * 
 *  u32 0x44894489 Sync
 *  u32 0x198c :: Track Length
 *  u32 dat[6544/4]
 *  u32 checksum :: Calculate EOR of all raw data including the track length
 * 
 * TRKTYP_eliminator_b data layout:
 * 
 *  u32 0x44894489 Sync
 *  u32 0x199c :: Track Length
 *  u32 dat[6560/4]
 *  u32 checksum :: Calculate EOR of all raw data including the track length
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct elminator_info {
    uint32_t len;
};

static void *eliminator_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct elminator_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], trackln, csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* track length */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &trackln);
        if (be32toh(trackln) != info->len)
            continue;

        sum = be32toh(raw[0]) ^ be32toh(raw[1]);

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);
        }
        sum &= 0x55555555u;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);

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

static void eliminator_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct elminator_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, info->len);
    sum = (uint32_t)info->len ^ ((uint32_t)info->len >> 1);
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        sum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
    }
    sum &= 0x55555555u;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, sum);
}

struct track_handler eliminator_a_handler = {
    .bytes_per_sector = 6544,
    .nr_sectors = 1,
    .write_raw = eliminator_write_raw,
    .read_raw = eliminator_read_raw,
    .extra_data = & (struct elminator_info) {
        .len = 0x198c
    }
};

struct track_handler eliminator_b_handler = {
    .bytes_per_sector = 6560,
    .nr_sectors = 1,
    .write_raw = eliminator_write_raw,
    .read_raw = eliminator_read_raw,
    .extra_data = & (struct elminator_info) {
        .len = 0x199c
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
