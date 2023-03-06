/*
 * disk/dizzy_dice.c
 *
 * Custom format as used on Dizzy Dice by Smash 16
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 track number - 0xffffff00 | tracknr
 *  u32 dat[ti->len/4]
 *  u32 checksum - raw data eor'd then & 0x55555555
 *
 * TRKTYP_dizzy_dice data layout:
 *  u8 sector_data[5120]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *dizzy_dice_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum, trk;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);

        if ((uint8_t)be32toh(trk) != tracknr)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);        
            sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);
        }
        sum &= 0x55555555;

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

static void dizzy_dice_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0xffffff00 | tracknr);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
    csum &= 0x55555555u;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler dizzy_dice_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = dizzy_dice_write_raw,
    .read_raw = dizzy_dice_read_raw
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
