/*
 * disk/world_championship_manager.c
 *
 * Custom format as used on World Championship Boxing Manager by Krisalis
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x8a51 :: Sync
 *  u16 0x2aaa :: padding
 *  u32 0xaaaaaaa5 :: padding - This value is checked
 *  u32 dat[5632/4]
 *  u32 checksum
 *
 * 
 * TRKTYP_world_championship_manager data layout:
 *  u8 sector_data[5632]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *world_championship_boxing_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x8a51)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* padding - check by loader */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaa5)
            continue;

        /* data */
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* game can corrupt the disk on track 40 if you loaded a 
        saved game and one did not exist */
        if (be32toh(csum) != sum) {
            if (tracknr != 40)
                goto fail;
            else
                trk_warn(ti, tracknr, "The track cheksums do not match!\nPossible Cause: loading a saved game and one does not exist, which can corrupted the original disk");
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

static void world_championship_boxing_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8A51);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaa5);

    /* data */
    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    /* cheksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler world_championship_boxing_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = world_championship_boxing_write_raw,
    .read_raw = world_championship_boxing_read_raw
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
