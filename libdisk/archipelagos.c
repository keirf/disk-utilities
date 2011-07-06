/******************************************************************************
 * disk/archipelagos.c
 * 
 * Custom format as used in Archipelagos by Logotron Entertainment.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  5 back-to-back sectors with explicit sector gap.
 *  Total encoded sector size, including gap, is 0x820 (2080) bytes.
 * RAW SECTOR:
 *  u8 0xa1,0xa1   :: 0x4489 sync marks
 *  u8 0xff
 *  u8 trk,sec+1
 *  u16 csum
 *  u8 data[1024]
 *  u8 gap[9]
 * MFM encoding:
 *  No even/odd split
 * 
 * TRKTYP_archipelagos data layout:
 *  u8 sector_data[5][1024]
 * 
 * Track 1 protection:
 * -------------------
 *  0x4454 sync mark
 *  u8 0x33  (6000 times)
 *  Protection track is long -- must be >= 107200 bits
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *archipelagos_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int i, valid_blocks = 0;

    if ( tracknr == 1 )
    {
        /*
         * No data worth analysing on the Archipelagos protection track.
         * We can construct it entirely synthetically in the read_mfm handler.
         */
        ti->data_bitoff = 1000;  /* somewhat arbitrary */
        ti->total_bits = 110000; /* likewise; but long enough */
        ti->bytes_per_sector = ti->nr_sectors = ti->len = 0;
        return block;
    }

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<ti->nr_sectors)-1)) )
    {
        uint32_t idx_off = s->index_offset - 31;
        uint16_t csum, w, *p;
        uint8_t sec;

        if ( s->word != 0x44894489 )
            continue;

        if ( stream_next_bits(s, 32) == -1 )
            goto done;
        if ( copylock_decode_word(s->word) != (0xff00 | tracknr) )
            continue;

        if ( stream_next_bits(s, 16) == -1 )
            goto done;
        sec = copylock_decode_word((uint16_t)s->word) - 1;
        if ( (sec >= ti->nr_sectors) || (valid_blocks & (1u<<sec)) )
            continue;

        if ( stream_next_bits(s, 32) == -1 )
            goto done;
        csum = copylock_decode_word(s->word);

        p = (uint16_t *)(block + sec * ti->bytes_per_sector);
        for ( i = 0; i < ti->bytes_per_sector/2; i++ )
        {
            if ( stream_next_bits(s, 32) == -1 )
                goto done;
            csum -= w = copylock_decode_word(s->word);
            *p++ = htons(w);
        }

        if ( csum )
            continue;

        valid_blocks |= 1u << sec;
        if ( !(valid_blocks & ((1u<<sec)-1)) )
            ti->data_bitoff = idx_off;
    }

done:
    if ( valid_blocks == 0 )
    {
        free(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;

    for ( i = 0; i < ti->nr_sectors; i++ )
        if ( valid_blocks & (1u << i) )
            break;
    ti->data_bitoff -= i * 0x820;

    return block;
}

static void archipelagos_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    if ( tracknr == 1 )
    {
        /* Long protection track. */
        tbuf_bits(tbuf, SPEED_AVG, TB_raw, 16, 0x4454);
        for ( i = 0; i < 6000; i++ )
            tbuf_bits(tbuf, SPEED_AVG, TB_all, 8, 0x33);
        return;
    }

    for ( i = 0; i < ti->nr_sectors; i++ )
    {
        uint16_t csum = 0;
        /* header */
        tbuf_bits(tbuf, SPEED_AVG, TB_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, TB_all, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, TB_all, 8, tracknr);
        tbuf_bits(tbuf, SPEED_AVG, TB_all, 8, i+1);
        /* csum */
        for ( j = 0; j < ti->bytes_per_sector/2; j++ )
            csum += ntohs(dat[j]);
        if ( !(ti->valid_sectors & (1u << i)) )
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, TB_all, 16, csum);
        /* data */
        for ( j = 0; j < 512; j++, dat++ )
            tbuf_bits(tbuf, SPEED_AVG, TB_all, 16, ntohs(*dat));
        /* gap */
        for ( j = 0; j < 9; j++ )
            tbuf_bits(tbuf, SPEED_AVG, TB_all, 8, 0);
    }
}

struct track_handler archipelagos_handler = {
    .name = "Archipelagos",
    .type = TRKTYP_archipelagos,
    .bytes_per_sector = 1024,
    .nr_sectors = 5,
    .write_mfm = archipelagos_write_mfm,
    .read_mfm = archipelagos_read_mfm
};
