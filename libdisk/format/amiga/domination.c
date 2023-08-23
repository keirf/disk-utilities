/*
 * disk/domination.c
 *
 * Custom format as used on Domination Gonzo Games
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 0x54892AAA
 *  u16 dat[5632/2]
 *  u16 0x4a89
 *  u16 padding
 *  u16 padding
 * 
 *  Did not see any sign of a checksum. 
 * 
 * TRKTYP_domination data layout:
 *  u8 sector_data[5648]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *domination_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[ti->len+2];
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x54892AAA)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, ti->len, dat, dat);

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4a89)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        dat[ti->len/2] = (uint16_t)s->word;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        dat[ti->len/2+1] = (uint16_t)s->word;

        stream_next_index(s);
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void domination_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint32_t sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x54892AAA);

    for (i = sum = 0; i < ti->len/2-7; i++)
        sum += be16toh(dat[i]);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4a89);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[ti->len/2]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[ti->len/2+1]);
}

struct track_handler domination_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = domination_write_raw,
    .read_raw = domination_read_raw
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
