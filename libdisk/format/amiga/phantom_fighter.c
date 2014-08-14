/*
 * disk/phantom_fighter.c
 * 
 * Custom format as used on Phantom Fighter by Emerald Software / Martech.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u16 0x5555
 *  struct {
 *    u16 dat[0x2ec]
 *    u32 0x????5555 :: Filler
 *  } [8]
 * The 8 data sections are compacted into a single 5984-word region.
 * This is even/odd decoded as a block, creating a 2992-word region:
 * 2991 words of data, followed by an ADD.W checksum.
 * 
 * TRKTYP_phantom_fighter data layout:
 *  u8 sector_data[5982]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *phantom_fighter_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[0x1760], csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;

        for (i = 0; i < 8; i++) {
            if (stream_next_bytes(s, &dat[0x2ec*i], 0x2ec*2) == -1)
                goto fail;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
        }

        mfm_decode_bytes(bc_mfm_even_odd, 0x1760, dat, dat);

        for (i = csum = 0; i < ti->len/2; i++)
            csum += be16toh(dat[i]);
        if (csum != be16toh(dat[ti->len/2]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void phantom_fighter_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);

    for (i = csum = 0; i < ti->len/2; i++)
        csum += be16toh(dat[i]);

    for (j = 0; j < 2; j++) {
        unsigned int type = j ? bc_mfm_odd : bc_mfm_even;
        for (i = 0; i < 4; i++) {
            tbuf_bytes(tbuf, SPEED_AVG, type, 2 * ((i == 3) ? 0x2eb : 0x2ec),
                       &dat[0x2ec*i]);
            if (i == 3)
                tbuf_bits(tbuf, SPEED_AVG, type, 16, csum);
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xffff);
        }
    }
}

struct track_handler phantom_fighter_handler = {
    .bytes_per_sector = 5982,
    .nr_sectors = 1,
    .write_raw = phantom_fighter_write_raw,
    .read_raw = phantom_fighter_read_raw
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
