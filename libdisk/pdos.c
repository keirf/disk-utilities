/******************************************************************************
 * disk/pdos.c
 * 
 * Rob Northen Computing Protected DOS (RNC PDOS) format.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x1448     :: MFM sync
 *  12 back-to-back sectors, 0x40a MFM bytes each
 * Decoded sector:
 *  u8 --          :: sector marker MFM sync 0x4891
 *  u8 sector      :: 0-11
 *  u8 track       :: 0-159
 *  u16 csum
 *  u8 data[512]
 *  u8 gap_words   :: usually 28(decimal), 0 on last sector
 * MFM encoding of sectors:
 *  <sector,track,csum>_even, <sector,track,csum>_odd
 *  <data>_even, <data>_odd
 *  Header long is EORed with disk key with bit 31 set. 
 *  Data longs are chain EORed with the disk key
 * Timings:
 *  Long track (~105500 bits), uniform density.
 * 
 * TRKTYP_pdos data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

extern uint16_t copylock_decode_word(uint32_t);
extern uint32_t mfm_decode_amigados(void *dat, unsigned int longs);

static void *pdos_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    char *block = memalloc(512 * 12);
    unsigned int i, j, valid_blocks = 0;
    uint32_t key;

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<6)-1)) )
    {
        uint8_t hdr[2*4], dat[2*512], skip;
        uint32_t k, *p, *q, csum;

        if ( (uint16_t)s->word != 0x1448 )
            continue;
        ti->data_bitoff = s->index_offset - 15;

        for ( i = 0; i < 12; i++ )
        {
            /* Check per-sector sync. */
            if ( stream_next_bits(s, 16) == -1 )
                goto done;
            if ( (uint16_t)s->word != 0x4891 )
                break;

            /* Read and MFM-decode the header and data areas. */
            if ( (stream_next_bytes(s, hdr, 2*4) == -1) ||
                 (stream_next_bytes(s, dat, 2*512) == -1) )
                goto done;
            mfm_decode_amigados(hdr, 4/4);
            csum = mfm_decode_amigados(dat, 512/4);
            csum = (uint16_t)(csum | (csum >> 15));

            /* Brute-force the key. */
            key = ((hdr[0] ^ i) & 0x7f) << 24;
            key |= (hdr[1] ^ tracknr) << 16;
            key |= (hdr[2] ^ (uint8_t)(csum>>8)) << 8;
            key |= hdr[3] ^ (uint8_t)csum;

            /* Decrypt and stash the data block. */
            k = key;
            p = (uint32_t *)dat;
            q = (uint32_t *)&block[i*512];
            for ( j = 0; j < 512/4; j++ )
            {
                k ^= ntohl(*p++);
                *q++ = htonl(k);
            }

            /* Skip the sector gap. */
            if ( stream_next_bits(s, 16) == -1 )
                goto done;
            skip = copylock_decode_word((uint16_t)s->word);
            if ( stream_next_bits(s, skip*16) == -1 )
                goto done;
        }

        if ( i == 12 )
        {
            valid_blocks = (1u << 12) - 1;
            break;
        }
    }

done:
    if ( valid_blocks == 0 )
    {
        free(block);
        return NULL;
    }

    ti->total_bits = 105500;
    ti->valid_sectors = valid_blocks;

    return block;
}

static void pdos_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint16_t *mfm = memalloc(6 + 6*513*2*2);
    unsigned int i, j;

    tbuf->start = ti->data_bitoff;
    tbuf->len = ti->total_bits;
    tbuf_init(tbuf);

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x4489);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_all, 16, 0xf000);

    for ( i = 0; i < 6; i++ )
    {
        uint16_t csum = 0;
        for ( j = 0; j < 512; j++ )
            csum += ntohs(dat[j]);
        if ( !(ti->valid_sectors & (1u << i)) )
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, csum);
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, csum);
        for ( j = 0; j < 512; j++ )
        {
            tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, ntohs(*dat));
            tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, ntohs(*dat));
            dat++;
        }
    }

    tbuf_finalise(tbuf);
}

struct track_handler pdos_handler = {
    .name = "RNC PDOS",
    .type = TRKTYP_rnc_pdos,
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_mfm = pdos_write_mfm,
    .read_mfm = pdos_read_mfm
};
