/******************************************************************************
 * disk/copylock.c
 * 
 * RobNorthen CopyLock protection track (Amiga).
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  514 decoded bytes per sector (excluding sector gap)
 *  Inter-sector gap of ~48 decoded zero bytes (48 MFM words).
 * Decoded Sector:
 *  <sync word>   :: Per-sector sync marker, see code for list
 *  u8 index      :: 0-11, must correspond to appropriate sync marker
 *  u8 data[512]
 * Data Bytes:
 *  data[n] = (data[n-1] << 1) | (RND?1:0)
 *  This relationship carries across sector boundaries. 
 * Sector 6:
 *  First 16 bytes interrupt random stream with signature "Rob Northen Comp"
 *  The random-byte relationship then continues uninterrupted at 17th byte.
 * MFM encoding:
 *  In place, no even/odd split.
 * Timings:
 *  Sync 0x8912 is 5% faster; Sync 0x8914 is 5% slower. All other bit cells
 *  are 2us, and total track length is exactly as usual (the short sector
 *  precisely balances the long sector).
 * 
 * TRKTYP_copylock data layout:
 *  u8 dat[11*512/8];
 *  Every 8th byte of the random stream, which is sufficient to reconstruct
 *  the entire stream.
 */

#include <libdisk/util.h>
#include "private.h"

static const uint16_t sync_list[] = {
    0x8a91, 0x8a44, 0x8a45, 0x8a51, 0x8912, 0x8911,
    0x8914, 0x8915, 0x8944, 0x8945, 0x8951 };
static const uint16_t sec6_sig[] = {
    0x526f, 0x6220, 0x4e6f, 0x7274, /* "Rob Northen Comp" */
    0x6865, 0x6e20, 0x436f, 0x6d70 };

uint16_t copylock_decode_word(uint32_t x)
{
    uint16_t y = 0;
    unsigned int i;
    for ( i = 0; i < 16; i++ )
    {
        y |= (x & 1) << i;
        x >>= 2;
    }
    return y;
}

static void *copylock_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    int i, j, sync = 0;
    uint32_t x=0, latency[11];
    uint8_t *info, *p, key=0;

    p = info = memalloc(ARRAY_SIZE(sync_list) * (512/8));

    while ( (stream_next_bit(s) != -1) &&
            (sync < ARRAY_SIZE(sync_list)) )
    {
        if ( (uint16_t)s->word != sync_list[sync] )
            continue;

        if ( sync == 0 )
            ti->data_bitoff = s->index_offset - 15;

        if ( stream_next_bits(s, 16) == -1 )
            goto fail;
        if ( copylock_decode_word((uint16_t)s->word) != sync )
            continue;

        s->latency = 0;
        for ( j = 0; j < 256; j++ )
        {
            if ( stream_next_bits(s, 32) == -1 )
                goto fail;
            x = copylock_decode_word((uint32_t)s->word);
            if ( (sync == 0) && (j == 0) )
                key = x>>9;
            if ( (sync == 6) && (j < ARRAY_SIZE(sec6_sig)) )
            {
                if ( x != sec6_sig[j] )
                    goto fail;
            }
            else
            {
                if ( (((x >> 7) ^ x) & 0xf8) ||
                     (((x>>9) ^ key) & 0x7f) )
                    goto fail;
                key = x;
                if ( (j & 3) == 0 )
                    *p++ = x >> 8;
            }
        }
        latency[sync] = s->latency;
        sync++;
    }

    *p++ = x << 1;

fail:
    if ( sync != ARRAY_SIZE(sync_list) )
    {
        memfree(info);
        return NULL;
    }

    for ( i = 0; i < ARRAY_SIZE(latency); i++ )
    {
        float d = (100.0 * ((int)latency[i] - (int)latency[5]))
            / (int)latency[5];
        switch ( i )
        {
        case 4:
            if ( d > -4.8 )
                printf("Copylock: Short sector is only %.2f%% different\n", d);
            break;
        case 6:
            if ( d < 4.8 )
                printf("Copylock: Long sector is only %.2f%% different\n", d);
            break;
        default:
            if ( (d < -2.0) || (d > 2.0) )
                printf("Copylock: Normal sector is %.2f%% different\n", d);
            break;
        }
    }

    ti->len = ti->nr_sectors * ti->bytes_per_sector/8;
    ti->valid_sectors = (1u << ti->nr_sectors) - 1;

    return info;
}

static void copylock_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i, j;
    uint8_t *dat = ti->dat;
    uint16_t word = *dat++;

    for ( i = 0; i < ARRAY_SIZE(sync_list); i++ )
    {
        uint16_t speed =
            i == 4 ? (DEFAULT_SPEED * 94) / 100 :
            i == 6 ? (DEFAULT_SPEED * 106) / 100 :
            DEFAULT_SPEED;
        tbuf_bits(tbuf, speed, TBUFDAT_raw, 16, sync_list[i]);
        tbuf_bits(tbuf, speed, TBUFDAT_all, 8, i);
        for ( j = 0; j < 512; j++ )
        {
            if ( (i == 6) && (j == 0) )
                for ( j = 0; j < 16; j += 2 )
                    tbuf_bits(tbuf, speed, TBUFDAT_all, 16, sec6_sig[j/2]);
            if ( !(j & 7) )
                word = (word << 8) | *dat++;
            tbuf_bits(tbuf, speed, TBUFDAT_all, 8, word >> (8 - (j&7)));
        }
        for ( j = 0; j < 48; j++ )
            tbuf_bits(tbuf, speed, TBUFDAT_all, 8, 0);
    }
}

struct track_handler copylock_handler = {
    .name = "Copylock",
    .type = TRKTYP_copylock,
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_mfm = copylock_write_mfm,
    .read_mfm = copylock_read_mfm
};
