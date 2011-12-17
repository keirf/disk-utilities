/*
 * disk/alienbreed_protection.c
 * 
 * Simple protection track as used in the original release of Alien Breed
 * by Team 17.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * FORMAT:
 *  u16 0x8924,0x8924 :: sync mark (poor - can be confused with MFM data)
 *  u32 dat0_even,dat0_odd
 *  u32 dat1_even,dat1_odd
 *  u32 dat2_even,dat2_odd
 *  u32 0xaaaaaaaa :: forever
 * 
 * The track is *not long*. This could be duplicated by ordinary Amiga h/w!
 * 
 * TRKTYP_alienbreed_protection data layout:
 *  u32 dat[3]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *alienbreed_protection_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = memalloc(3 * sizeof(uint32_t));
    uint32_t x[2];
    unsigned int i;

    while (stream_next_bit(s) != -1) {
        if (s->word != 0x89248924)
            continue;

        ti->data_bitoff = s->index_offset - 31;

        /* Get the data longs. */
        for (i = 0; i < 3; i++) {
            stream_next_bytes(s, x, sizeof(x));
            mfm_decode_amigados(x, 1);
            dat[i] = x[0];
        }

        /* Check for a long sequence of zeroes */
        for (i = 0; i < 1000; i++) {
            stream_next_bits(s, 32);
            if (copylock_decode_word(s->word) != 0)
                break;
        }
        if (i == 1000)
            goto found;
    }

    memfree(dat);
    return NULL;

found:
    ti->len = 3 * sizeof(uint32_t);
    return dat;
}

static void alienbreed_protection_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x89248924);
    for (i = 0; i < 3; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
    for (i = 0; i < 1000; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 32, 0);
}

struct track_handler alienbreed_protection_handler = {
    .write_mfm = alienbreed_protection_write_mfm,
    .read_mfm = alienbreed_protection_read_mfm
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
