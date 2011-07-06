/******************************************************************************
 * disk/rainbird.c
 * 
 * Custom format as used by various Rainbird releases:
 *   Betrayal
 *   Carrier Command
 *   Midwinter
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Track header
 *  u32 long
 *  u32 csum
 *  u32 data[10*512/4]
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum.
 * 
 * TRKTYP_rainbird data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *rainbird_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = NULL;

    while ( (stream_next_bit(s) != -1) && !block )
    {
        uint32_t raw_dat[2*ti->len/4], hdr, csum;
        uint32_t idx_off = s->index_offset - 15;

        if ( (uint16_t)s->word != 0x4489 )
            continue;

        if ( stream_next_bits(s, 16) == -1 )
            goto done;
        if ( (uint16_t)s->word != 0x4489 )
            continue;

        ti->data_bitoff = idx_off;

        if ( stream_next_bytes(s, raw_dat, 16) == -1 )
            goto done;
        mfm_decode_amigados(&raw_dat[0], 1);
        mfm_decode_amigados(&raw_dat[2], 1);
        hdr = ntohl(raw_dat[0]);
        csum = ntohl(raw_dat[2]);

        if ( hdr != (0xffffff00u | tracknr) )
            continue;

        if ( stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1 )
            goto done;
        if ( (csum ^= mfm_decode_amigados(raw_dat, ti->len/4)) != 0 )
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
    }

done:
    if ( block )
        ti->valid_sectors = 1;

    return block;
}

static void rainbird_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t track, csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, TB_raw, 32, 0x44894489);

    track = (~0u << 8) | tracknr;
    tbuf_bits(tbuf, SPEED_AVG, TB_even_odd, 32, track);

    for ( i = 0; i < ti->len/4; i++ )
        csum ^= ntohl(dat[i]);
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    tbuf_bits(tbuf, SPEED_AVG, TB_even_odd, 32, csum);

    tbuf_bytes(tbuf, SPEED_AVG, TB_even_odd, ti->len, dat);
}

struct track_handler rainbird_handler = {
    .type = TRKTYP_rainbird,
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_mfm = rainbird_write_mfm,
    .read_mfm = rainbird_read_mfm
};
