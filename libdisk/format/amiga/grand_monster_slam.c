/*
 * disk/grand_monster_slam.c
 * 
 * Custom format as used on Grand Monster Slam by Rainbow Arts.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489,0x2aaa :: Sync
 *  u8  dat[0x1600]          :: Odd
 *  u32 csum                 :: Odd
 *  u8  dat[0x1600]          :: Even
 *  u32 csum                 :: Even
 * Checksum is NEG.L of sum of all data words
 * 
 * TRKTYP_grand_monster_slam data layout:
 *  u8 sector_data[512*11]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *grand_monster_slam_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[0x1604];
        uint32_t csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0x44892aaa)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        for (i = csum = 0; i < 0xb00; i++)
            csum += (uint32_t)be16toh(dat[i]);
        csum += ((uint32_t)be16toh(dat[0xb00]) << 16) | be16toh(dat[0xb01]);
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void grand_monster_slam_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint32_t csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/2; i++)
        csum += (uint32_t)be16toh(dat[i]);
    csum = -csum;

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd, 32, csum);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even, 32, csum);
}

struct track_handler grand_monster_slam_handler = {
    .bytes_per_sector = 512*11,
    .nr_sectors = 1,
    .write_raw = grand_monster_slam_write_raw,
    .read_raw = grand_monster_slam_read_raw
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
