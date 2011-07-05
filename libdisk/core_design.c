/******************************************************************************
 * disk/core_design.c
 * 
 * Custom format as used by various releases by Core Design:
 *   Jaguar XJ220
 *   Premiere
 *   Thunderhawk AH-73M
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
 * TRKTYP_core_design data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *core_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t mfm[2], csum, *block = NULL;
    unsigned int i;

    while ( (stream_next_bit(s) != -1) && !block )
    {
        if ( (uint16_t)s->word != 0x8915 )
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
            goto done;
        mfm_decode_amigados(mfm, 4/4);
        csum = ntohl(mfm[0]);

        block = memalloc(ti->len);

        for ( i = 0; i < ti->len/4; i++ )
        {
            if ( stream_next_bytes(s, mfm, sizeof(mfm)) == -1 )
                goto done;
            mfm_decode_amigados(mfm, 4/4);
            csum -= ntohl(block[i] = mfm[0]);
        }

        if ( csum )
        {
            memfree(block);
            block = NULL;
        }
    }

done:
    if ( block != NULL )
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;

    return block;
}

static void core_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x8915);

    for ( i = 0; i < ti->len/4; i++ )
        csum += ntohl(dat[i]);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, csum);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, csum);

    for ( i = 0; i < ti->len/4; i++ )
    {
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, ntohl(dat[i]));
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, ntohl(dat[i]));
    }
}

struct track_handler core_design_handler = {
    .name = "Core Design",
    .type = TRKTYP_core_design,
    .bytes_per_sector = 11*512,
    .nr_sectors = 1,
    .write_mfm = core_write_mfm,
    .read_mfm = core_read_mfm
};
