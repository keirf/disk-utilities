/*
 * disk/nightdawn.c
 *
 * Custom format as used in Nightdawn
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489
 *  u32 0x54892aaa
 *  u32 data[5888]
 *  u32 0x4a892aaa
 *
 * TRKTYP_nightdawn data layout:
 *  u8 sector_data[5888]
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *nightdawn_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[(ti->len/4)*2];
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x54892aaa)
            continue;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4a892aaa)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void nightdawn_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x54892aaa);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4a892aaa);
}

struct track_handler nightdawn_handler = {
    .bytes_per_sector = 5888,
    .nr_sectors = 1,
    .write_raw = nightdawn_write_raw,
    .read_raw = nightdawn_read_raw
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
