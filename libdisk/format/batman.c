/*
 * disk/batman.c
 * 
 * Custom format as used on Batman The Caped Crusader by Ocean.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 * 12 sectors back-to-back (0x418 raw mfm bytes each):
 *  u16 0x8944
 *  u8  0
 *  u8  (tracknr-2)^1, sector, to_gap, mbz :: encoded as even/odd long
 *  u32 csum :: encoded as even/odd long
 *  u8  dat[512] :: encoded as even/odd block
 *  u16 0
 * 
 * TRKTYP_batman data layout:
 *  u8 sector_data[12][512]
 *  u8 first_sector
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

struct hdr {
    uint8_t track, sector, to_gap, mbz;
};

static uint32_t checksum(uint16_t *dat)
{
    unsigned int i;
    uint32_t sum;

    for (i = sum = 0; i < 256; i++) {
        uint32_t x = ntohs(dat[i]);
        x <<= i & 15;
        x |= x >> 16;
        sum += x;
    }

    sum = (int16_t)sum;
    sum <<= 8;
    if (sum & 0x80000000u)
        sum |= 0xffu;

    return sum;
}

static void *batman_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int valid_blocks = 0, max_to_gap = 0;

    block = memalloc((12*512) + 1);

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        struct hdr hdr;
        uint32_t csum, idx_off;
        uint16_t dat[512];

        if (s->word != 0x8944aaaa)
            continue;

        idx_off = s->index_offset - 31;

        if (stream_next_bytes(s, dat, 8) == -1)
            break;
        mfm_decode_bytes(MFM_even_odd, 4, dat, &hdr);

        if ((hdr.track != ((tracknr-2)^1)) ||
            (hdr.mbz != 0) ||
            (hdr.to_gap < 1) || (hdr.to_gap > 12) ||
            (hdr.sector >= ti->nr_sectors) ||
            (valid_blocks & (1u<<hdr.sector)))
            continue;

        if (stream_next_bytes(s, dat, 8) == -1)
            break;
        mfm_decode_bytes(MFM_even_odd, 4, dat, dat);
        csum = ntohl(*(uint32_t *)dat);

        if (stream_next_bytes(s, dat, 2*512) == -1)
            break;
        mfm_decode_bytes(MFM_even_odd, 512, dat, dat);

        if (checksum(dat) != csum)
            continue;

        memcpy(&block[hdr.sector*512], dat, 512);
        valid_blocks |= 1u << hdr.sector;

        /*
         * Look for the first written sector after track gap (or close to it
         * as possible) to determine the track-data offset and first sector.
         */
        if (hdr.to_gap > max_to_gap) {
            max_to_gap = hdr.to_gap;
            ti->data_bitoff = idx_off - (12-hdr.to_gap)*1048*8;
            block[12*512] = (hdr.sector + hdr.to_gap) % 12;
        }
    }

    if (valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;
    ti->total_bits = 105500;

    return block;
}

static void batman_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct hdr hdr;
    uint32_t csum;
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i, first_sector = ti->dat[12*512];

    for (i = 0; i < ti->nr_sectors; i++) {
        /* sync mark */
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8944);
        /* filler */
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
        /* header info */
        hdr.track = (tracknr-2)^1;
        hdr.sector = (first_sector + i) % 12;
        hdr.to_gap = 12-i;
        hdr.mbz = 0;
        tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, 4, &hdr);
        /* data checksum */
        dat = (uint16_t *)&ti->dat[512*hdr.sector];
        csum = checksum(dat);
        if (!(ti->valid_sectors & (1u << i)))
            csum ^= 1; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);
        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, 512, dat);
        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0);
    }
}

struct track_handler batman_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_mfm = batman_write_mfm,
    .read_mfm = batman_read_mfm
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
