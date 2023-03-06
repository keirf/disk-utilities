/*
 * disk/alien_legion.c
 *
 * Custom format as used on Alien Legion by Gainstar
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x9521 Sync
 *  u16 0x2aaa
 *  u32 dat[ti->len/4]
 *
 * TRKTYP_alien_legion data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *alien_legion_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
        }

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

static void alien_legion_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
    }
}

struct track_handler alien_legion_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = alien_legion_write_raw,
    .read_raw = alien_legion_read_raw
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
