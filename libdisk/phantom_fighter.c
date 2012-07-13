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
#include "private.h"

#include <arpa/inet.h>

static void *phantom_fighter_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[0x1760], csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;

        for (i = 0; i < 8; i++) {
            if (stream_next_bytes(s, &dat[0x2ec*i], 0x2ec*2) == -1)
                goto fail;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
        }

        mfm_decode_bytes(MFM_even_odd, 0x1760, dat, dat);

        for (i = csum = 0; i < ti->len/2; i++)
            csum += ntohs(dat[i]);
        if (csum != ntohs(dat[ti->len/2]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        stream_next_index(s);
        return block;
    }

fail:
    stream_next_index(s);
    return NULL;
}

static void phantom_fighter_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0xff);

    for (i = csum = 0; i < ti->len/2; i++)
        csum += ntohs(dat[i]);

    for (j = 0; j < 2; j++) {
        unsigned int type = j ? MFM_odd : MFM_even;
        for (i = 0; i < 4; i++) {
            tbuf_bytes(tbuf, SPEED_AVG, type, 2 * ((i == 3) ? 0x2eb : 0x2ec),
                       &dat[0x2ec*i]);
            if (i == 3)
                tbuf_bits(tbuf, SPEED_AVG, type, 16, csum);
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0xffff);
        }
    }
}

struct track_handler phantom_fighter_handler = {
    .bytes_per_sector = 5982,
    .nr_sectors = 1,
    .write_mfm = phantom_fighter_write_mfm,
    .read_mfm = phantom_fighter_read_mfm
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
