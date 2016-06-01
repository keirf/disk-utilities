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

static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

/* TRKTYP_nightdawn_prot:
 *  u16 0x5541 :: sync
 *  Rest of track is (MFM-encoded) 0xff
 *  The check starts from the offset of $a which includes the sync.
 *  The protection checks for > 10000 consecutive bytes of the same
 *  value (MFM-encoded) 0xff. */

static void *nightdawn_prot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x55554155)
            continue;

        if (!check_sequence(s, 0x2710/2, 0xff))
            continue;

        ti->total_bits = 101000;
        return memalloc(0);
    }

    return NULL;
}

static void nightdawn_prot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55554155);
    for (i = 0; i < 0x2710/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
}

struct track_handler nightdawn_prot_handler = {
    .write_raw = nightdawn_prot_write_raw,
    .read_raw = nightdawn_prot_read_raw
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
