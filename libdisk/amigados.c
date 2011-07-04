/******************************************************************************
 * disk/amigados.c
 * 
 * AmigaDOS disk format.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  544 decoded bytes per sector (including sector gap).
 *  11 back-to-back sectors, as encoded below (explicit gap included).
 * Decoded Sector:
 *  u8 0x00,0x00 :: Sector gap
 *  u8 0xa1,0xa1 :: Sync header (encoded as 0x4489 0x4489)
 *  u8 format    :: Always 0xff
 *  u8 track     :: 0-159
 *  u8 sector    :: 0-10
 *  u8 sec_to_gap:: 1-11
 *  u8 label[16] :: usually zero
 *  u32 hdr_csum :: (XOR raw MFM) & 0x55555555
 *  u32 dat_csum
 *  u8 data[512]
 * MFM encoding:
 *  u16 0xaaaa,0xaaaa
 *  u16 0x4489,0x4489
 *  u32 info_even,info_odd
 *  u8  label_even[16],label_odd[16]
 *  u32 hdr_csum_even,hdr_csum_odd
 *  u32 dat_csum_even,dat_csum_odd
 *  u8  data_even[512],data_odd[512]
 * 
 * TRKTYP_amigados data layout:
 *  u8 sector_data[11][512]
 * 
 * TRKTYP_amigados_labelled data layout:
 *  struct sector {
 *   u8 label[16];
 *   u8 data[512];
 *  } sector[11];
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

/* AmigaDOS logical format properties. */
#define ADOS_BYTES_PER_BLOCK   512
#define ADOS_BLOCKS_PER_TRACK   11

struct ados_hdr {
    uint8_t  format, track, sector, sectors_to_gap;
    uint8_t  lbl[16];
    uint32_t hdr_checksum;
    uint32_t dat_checksum;
};

uint32_t mfm_decode_amigados(void *dat, unsigned int longs)
{
    uint32_t *even = dat, *odd = even + longs, csum = 0;
    unsigned int i;

    for ( i = 0; i < longs; i++, even++, odd++ )
    {
        csum ^= *even ^ *odd;
        *even = ((*even & 0x55555555u) << 1) | (*odd & 0x55555555u);
    }

    return ntohl(csum & 0x55555555);
}

static void *ados_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    char *block, *p;
    unsigned int i, j, valid_blocks = 0, labelled_blocks = 0;

    block = memalloc((ADOS_BYTES_PER_BLOCK + 16) * ADOS_BLOCKS_PER_TRACK);
    for ( i = 0; i < ADOS_BYTES_PER_BLOCK * ADOS_BLOCKS_PER_TRACK / 4; i++ )
        memcpy((uint32_t *)block + i, "NDOS", 4);

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<ADOS_BLOCKS_PER_TRACK)-1)) )
    {
        struct ados_hdr ados_hdr;
        char raw_mfm_dat[2*(sizeof(struct ados_hdr)+512)];
        uint32_t csum, idx_off = s->index_offset;

        if ( s->word != 0x44894489 )
            continue;

        if ( stream_next_bytes(s, raw_mfm_dat, sizeof(raw_mfm_dat)) == -1 )
            break;

        csum = mfm_decode_amigados(&raw_mfm_dat[2*0], 4/4);
        memcpy(&ados_hdr, &raw_mfm_dat[0], 4);
        csum ^= mfm_decode_amigados(&raw_mfm_dat[2*4], 16/4);
        memcpy(ados_hdr.lbl, &raw_mfm_dat[2*4], 16);
        csum ^= mfm_decode_amigados(&raw_mfm_dat[2*20], 4/4);
        ados_hdr.hdr_checksum = ((uint32_t *)raw_mfm_dat)[2*20/4];
        if ( csum != 0 )
            continue;

        csum = mfm_decode_amigados(&raw_mfm_dat[2*24], 4/4);
        ados_hdr.dat_checksum = ((uint32_t *)raw_mfm_dat)[2*24/4];
        csum ^= mfm_decode_amigados(
            &raw_mfm_dat[2*28], ADOS_BYTES_PER_BLOCK/4);
        if ( csum != 0 )
            continue;

        if ( (ados_hdr.format != 0xffu) ||
             (ados_hdr.track != tracknr) ||
             (ados_hdr.sector >= ADOS_BLOCKS_PER_TRACK) ||
             (valid_blocks & (1u<<ados_hdr.sector)) )
            continue;

        for ( j = 0; j < 16; j++ )
            if ( ados_hdr.lbl[j] != 0 )
                labelled_blocks |= 1u << ados_hdr.sector;

        p = block + ados_hdr.sector * (ADOS_BYTES_PER_BLOCK + 16);
        memcpy(p, ados_hdr.lbl, 16);
        memcpy(p+16, &raw_mfm_dat[2*28], ADOS_BYTES_PER_BLOCK);

        if ( (ados_hdr.sector == 0) ||
             !(valid_blocks & (1u << (ados_hdr.sector-1))) )
            ti->data_bitoff = idx_off;

        valid_blocks |= 1u << ados_hdr.sector;
    }

    if ( valid_blocks == 0 )
    {
        memfree(block);
        return NULL;
    }

    if ( !labelled_blocks )
        for ( i = 0; i < ADOS_BLOCKS_PER_TRACK; i++ )
            memmove(block + i * ADOS_BYTES_PER_BLOCK,
                    block + i * (ADOS_BYTES_PER_BLOCK + 16) + 16,
                    ADOS_BYTES_PER_BLOCK);

    ti->type = labelled_blocks ? TRKTYP_amigados_labelled : TRKTYP_amigados;
    init_track_info_from_handler_info(ti, handlers[ti->type]);

    ti->valid_sectors = valid_blocks;

    for ( i = 0; i < ADOS_BLOCKS_PER_TRACK; i++ )
        if ( valid_blocks & (1u << i) )
            break;
    ti->data_bitoff -= i * 544 + 31;

    return block;
}

static void write_csum(struct track_buffer *tbuf, uint32_t csum)
{
    /* crappy parity-based checksum, only uses half the checksum bits! */
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, csum);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, csum);
}

static void ados_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    uint8_t lbl[16] = { 0 };
    uint8_t *dat = ti->dat;
    unsigned int i, j;
    uint32_t csum, info = (0xffu << 24) | (tracknr << 16);

    tbuf->start = ti->data_bitoff;
    tbuf->len = ti->total_bits;
    tbuf_init(tbuf);

    for ( i = 0; i < ADOS_BLOCKS_PER_TRACK; i++ )
    {
        /* sync mark */
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 32, 0x44894489);
        /* info */
        info &= ~0xffffu;
        info |= (i << 8) | (11-i);
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 32, info);
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 32, info);
        /* lbl */
        if ( ti->type == TRKTYP_amigados_labelled )
        {
            memcpy(lbl, dat, 16);
            dat += 16;
        }
        tbuf_bytes(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, lbl);
        tbuf_bytes(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, lbl);
        /* header checksum */
        csum = info;
        for ( j = 0; j < 4; j++ )
            csum ^= ntohl(((uint32_t *)lbl)[j]);
        write_csum(tbuf, csum);
        /* data checksum */
        csum = 0;
        for ( j = 0; j < 128; j++ )
            csum ^= ntohl(((uint32_t *)dat)[j]);
        if ( !(ti->valid_sectors & (1u << i)) )
            csum ^= 1; /* bad checksum for an invalid sector */
        write_csum(tbuf, csum);
        /* data */
        tbuf_bytes(tbuf, DEFAULT_SPEED, TBUFDAT_even, 512, dat);
        tbuf_bytes(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 512, dat);
        dat += 512;
        /* gap */
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_all, 16, 0);
    }

    tbuf_finalise(tbuf);
}

struct track_handler amigados_handler = {
    .name = "AmigaDOS",
    .type = TRKTYP_amigados,
    .bytes_per_sector = ADOS_BYTES_PER_BLOCK,
    .nr_sectors = ADOS_BLOCKS_PER_TRACK,
    .write_mfm = ados_write_mfm,
    .read_mfm = ados_read_mfm
};

struct track_handler amigados_labelled_handler = {
    .name = "AmigaDOS w/Labels",
    .type = TRKTYP_amigados_labelled,
    .bytes_per_sector = ADOS_BYTES_PER_BLOCK + 16,
    .nr_sectors = ADOS_BLOCKS_PER_TRACK,
    .write_mfm = ados_write_mfm,
    .read_mfm = ados_read_mfm
};
