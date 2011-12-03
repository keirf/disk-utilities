/*
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
 * TRKTYP_amigados_extended data layout:
 *  struct sector {
 *   u32 sync;
 *   u8 hdr[4];
 *   u8 label[16];
 *   u8 data[512];
 *  } sector[11];
 * 
 * The extended form is used by various games:
 *   New Zealand Story: stashes a custom data checksum in the label area
 *   Graftgold (Fire & Ice, Uridium 2, ...): store cyl# in place of track#
 *   Z Out: custom sync on track 1
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

/* Sector data sizes for amigados and amigados_extended handlers. */
#define STD_SEC 512
#define EXT_SEC (STD_SEC + 24)

const static uint32_t syncs[] = {
    0x44894489,
    0x45214521  /* Z Out, track 1 */
};

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

    for (i = 0; i < longs; i++, even++, odd++) {
        csum ^= *even ^ *odd;
        *even = ((*even & 0x55555555u) << 1) | (*odd & 0x55555555u);
    }

    return ntohl(csum & 0x55555555);
}

static void *ados_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block, *p;
    unsigned int i, valid_blocks = 0, extended_blocks = 0;

    block = memalloc(EXT_SEC * ti->nr_sectors);
    for (i = 0; i < EXT_SEC * ti->nr_sectors / 4; i++)
        memcpy((uint32_t *)block + i, "NDOS", 4);

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        struct ados_hdr ados_hdr;
        char raw_mfm_dat[2*(sizeof(struct ados_hdr)+STD_SEC)];
        uint32_t sync = s->word, csum, idx_off = s->index_offset - 31;

        for (i = 0; i < ARRAY_SIZE(syncs); i++)
            if (sync == syncs[i])
                break;
        if (i == ARRAY_SIZE(syncs))
            continue;

        if (stream_next_bytes(s, raw_mfm_dat, sizeof(raw_mfm_dat)) == -1)
            break;

        csum = mfm_decode_amigados(&raw_mfm_dat[2*0], 4/4);
        memcpy(&ados_hdr, &raw_mfm_dat[0], 4);
        csum ^= mfm_decode_amigados(&raw_mfm_dat[2*4], 16/4);
        memcpy(ados_hdr.lbl, &raw_mfm_dat[2*4], 16);
        csum ^= mfm_decode_amigados(&raw_mfm_dat[2*20], 4/4);
        ados_hdr.hdr_checksum = ((uint32_t *)raw_mfm_dat)[2*20/4];
        if (csum != 0)
            continue;

        csum = mfm_decode_amigados(&raw_mfm_dat[2*24], 4/4);
        ados_hdr.dat_checksum = ((uint32_t *)raw_mfm_dat)[2*24/4];
        csum ^= mfm_decode_amigados(&raw_mfm_dat[2*28], STD_SEC/4);
        if (csum != 0)
            continue;

        if ((ados_hdr.sector >= ti->nr_sectors) ||
            (valid_blocks & (1u<<ados_hdr.sector)))
            continue;

        /* Detect non-standard header info. */
        if ((ados_hdr.format != 0xffu) ||
            (ados_hdr.track != tracknr) ||
            (sync != syncs[0]))
            extended_blocks |= 1u << ados_hdr.sector;
        for (i = 0; i < 16; i++)
            if (ados_hdr.lbl[i] != 0)
                extended_blocks |= 1u << ados_hdr.sector;

        p = block + ados_hdr.sector * EXT_SEC;
        *(uint32_t *)p = htonl(sync);
        memcpy(p+4, &ados_hdr, 4);
        memcpy(p+8, ados_hdr.lbl, 16);
        memcpy(p+24, &raw_mfm_dat[2*28], STD_SEC);

        if (!(valid_blocks & ((1u<<ados_hdr.sector)-1)))
            ti->data_bitoff = idx_off;

        valid_blocks |= 1u << ados_hdr.sector;
    }

    if (valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    if (!extended_blocks)
        for (i = 0; i < ti->nr_sectors; i++)
            memmove(block + i * STD_SEC,
                    block + i * EXT_SEC + (EXT_SEC - STD_SEC),
                    STD_SEC);

    init_track_info(
        ti, extended_blocks ? TRKTYP_amigados_extended : TRKTYP_amigados);

    ti->valid_sectors = valid_blocks;

    for (i = 0; i < ti->nr_sectors; i++)
        if (valid_blocks & (1u << i))
            break;
    ti->data_bitoff -= i * 544*8*2;

    return block;
}

static void write_csum(struct track_buffer *tbuf, uint32_t csum)
{
    /* crappy parity-based checksum, only uses half the checksum bits! */
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    tbuf_bits(tbuf, SPEED_AVG, TB_even_odd, 32, csum);
}

static void ados_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t lbl[16] = { 0 };
    uint8_t *dat = ti->dat;
    unsigned int i, j;
    uint32_t sync, csum, info;

    for (i = 0; i < ti->nr_sectors; i++) {
        sync = syncs[0];
        info = (0xffu << 24) | (tracknr << 16) | (i << 8) | (11-i);

        if (ti->type == TRKTYP_amigados_extended) {
            sync = ntohl(*(uint32_t *)&dat[0]);
            info = ntohl(*(uint32_t *)&dat[4]);
            memcpy(lbl, &dat[8], 16);
            dat += 24;
        }

        /* sync mark */
        tbuf_bits(tbuf, SPEED_AVG, TB_raw, 32, sync);
        /* info */
        tbuf_bits(tbuf, SPEED_AVG, TB_even_odd, 32, info);
        /* lbl */
        tbuf_bytes(tbuf, SPEED_AVG, TB_even_odd, 16, lbl);
        /* header checksum */
        csum = info;
        for (j = 0; j < 4; j++)
            csum ^= ntohl(((uint32_t *)lbl)[j]);
        write_csum(tbuf, csum);
        /* data checksum */
        csum = 0;
        for (j = 0; j < 128; j++)
            csum ^= ntohl(((uint32_t *)dat)[j]);
        if (!(ti->valid_sectors & (1u << i)))
            csum ^= 1; /* bad checksum for an invalid sector */
        write_csum(tbuf, csum);
        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, TB_even_odd, STD_SEC, dat);
        dat += 512;
        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, TB_all, 16, 0);
    }
}

struct track_handler amigados_handler = {
    .bytes_per_sector = STD_SEC,
    .nr_sectors = 11,
    .write_mfm = ados_write_mfm,
    .read_mfm = ados_read_mfm
};

struct track_handler amigados_extended_handler = {
    .bytes_per_sector = EXT_SEC,
    .nr_sectors = 11,
    .write_mfm = ados_write_mfm,
    .read_mfm = ados_read_mfm
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
