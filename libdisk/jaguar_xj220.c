/******************************************************************************
 * disk/jaguar_xj220.c
 * 
 * Custom format as used by Jaguar XJ220 by Core Design.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8915 :: Sync
 *  u32 checksum
 *  u32 data[11*512/4]
 *  Checksum is sum of all decoded longs
 * MFM encoding:
 *  Alternating even/odd longs
 * 
 * TRKTYP_jaguar_xj220 data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *jaguar_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint32_t *block = memalloc(ti->len);
    unsigned int i, j, k, valid_blocks = 0, bad;

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<ti->nr_sectors)-1)) )
    {
        uint32_t mfm[2], csum;
        uint32_t nr_valid = 0;

        if ( (uint16_t)s->word != 0x8915 )
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
            goto done;
        mfm_decode_amigados(mfm, 4/4);
        csum = ntohl(mfm[0]);

        for ( i = 0; i < ti->nr_sectors*ti->bytes_per_sector/4; i++ )
        {
            if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
                goto done;
            mfm_decode_amigados(mfm, 4/4);
            csum -= ntohl(block[i] = mfm[0]);
        }

        if ( csum == 0 )
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

static void jaguar_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf->start = ti->data_bitoff;
    tbuf->len = ti->total_bits;
    tbuf_init(tbuf);

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x8915);

    for ( i = 0; i < ti->nr_sectors*ti->bytes_per_sector/4; i++ )
        csum += ntohl(dat[i]);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, csum);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, csum);

    for ( i = 0; i < ti->nr_sectors*ti->bytes_per_sector/4; i++ )
    {
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, ntohl(dat[i]));
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, ntohl(dat[i]));
    }

    tbuf_finalise(tbuf);
}

struct track_handler jaguar_xj220_handler = {
    .name = "Jaguar XJ220",
    .type = TRKTYP_jaguar_xj220,
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_mfm = jaguar_write_mfm,
    .read_mfm = jaguar_read_mfm
};
