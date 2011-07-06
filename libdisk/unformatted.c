/******************************************************************************
 * disk/unformatted.c
 * 
 * Unformatted (white noise) tracks.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#define SCAN_SECTOR_BITS 1000
#define SECTOR_BAD_THRESH (SCAN_SECTOR_BITS/10)

static void *unformatted_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    unsigned int bad = 0, nr_zero = 0, i = 0;

    /*
     * Scan for bit sequences that break the MFM encoding rules.
     * Random noise will obviously do this a *lot*.
     */
    while ( stream_next_bit(s) != -1 )
    {
        if ( s->word & 1 )
        {
            if ( !nr_zero )
                bad++;
            nr_zero = 0;
        }
        else if ( ++nr_zero > 3 )
            bad++;

        if ( ++i >= SCAN_SECTOR_BITS )
        {
            if ( bad < SECTOR_BAD_THRESH )
                return NULL;
            bad = nr_zero = i = 0;
        }
    }

    ti->total_bits = TRK_WEAK;

    return memalloc(1); /* dummy */
}

static void unformatted_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;
    int speed_delta = 200;
    uint8_t byte = 0;

    tbuf->start = 0;
    tbuf->len = (120000 * ((rand() & 255) + 1000 - 128)) / 1000;
    tbuf_init(tbuf);

    for ( i = 0; i < tbuf->len; i++ )
    {
        byte <<= 1;
        byte |= rand() & 1;
        if ( (i & 7) == 7 )
        {
            tbuf_bits(tbuf, SPEED_AVG + speed_delta, TB_raw, 8, byte);
            speed_delta = -speed_delta;
        }
    }
}

struct track_handler unformatted_handler  = {
    .write_mfm = unformatted_write_mfm,
    .read_mfm = unformatted_read_mfm
};
