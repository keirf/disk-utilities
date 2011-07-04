/******************************************************************************
 * disk/gremlin.c
 * 
 * Custom format as used by various Gremlin Graphics releases:
 *   Lotus I, II, and III
 *   Harlequin
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489,0x4489
 *  u16 0x5555
 *  u16 data[12*512/2]
 *  u16 csum
 *  u16 trk
 *  Checksum is sum of all decoded words
 *  Sides 0 and 1 of disk are inverted from normal.
 * MFM encoding:
 *  Alternating odd/even words
 * 
 * TRKTYP_gremlin data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *gremlin_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint16_t *block = memalloc(ti->len);
    unsigned int i, j, k, valid_blocks = 0, bad;

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<ti->nr_sectors)-1)) )
    {
        uint16_t mfm[2], csum = 0, trk;
        uint32_t nr_valid = 0;
        uint32_t idx_off = s->index_offset - 15;

        if ( (uint16_t)s->word != 0x4489 )
            continue;
        if ( stream_next_bits(s, 32) == -1 )
            goto done;
        if ( s->word != 0x44894489 )
            continue;
        if ( stream_next_bits(s, 16) == -1 )
            goto done;
        if ( (uint16_t)s->word != 0x5555 )
            continue;

        ti->data_bitoff = idx_off;

        for ( i = 0; i < ti->nr_sectors*ti->bytes_per_sector/2; i++ )
        {
            if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
                goto done;
            block[i] = (mfm[0] & 0x5555u) | ((mfm[1] & 0x5555u) << 1);
            csum += ntohs(block[i]);
        }

        if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
            goto done;
        csum -= ntohs((mfm[0] & 0x5555u) | ((mfm[1] & 0x5555u) << 1));

        if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
            goto done;
        trk = ntohs((mfm[0] & 0x5555u) | ((mfm[1] & 0x5555u) << 1));

        if ( (csum == 0) && (tracknr == (trk^1)) )
            valid_blocks = (1u << ti->nr_sectors) - 1;
    }

done:
    if ( valid_blocks == 0 )
    {
        free(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;

    return block;
}

static void gremlin_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint16_t csum = 0, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf->start = ti->data_bitoff;
    tbuf->len = ti->total_bits;
    tbuf_init(tbuf);

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x4489);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x4489);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x4489);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x5555);

    for ( i = 0; i < ti->nr_sectors*ti->bytes_per_sector/2; i++ )
    {
        csum += ntohs(dat[i]);
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, ntohs(dat[i]));
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, ntohs(dat[i]));
    }

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, csum);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, csum);

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, tracknr^1);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, tracknr^1);

    tbuf_finalise(tbuf);
}

struct track_handler gremlin_handler = {
    .name = "Gremlin Graphics",
    .type = TRKTYP_gremlin,
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_mfm = gremlin_write_mfm,
    .read_mfm = gremlin_read_mfm
};
