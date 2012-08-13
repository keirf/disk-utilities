/*
 * disk/sega_system_24.c
 * 
 * Custom IBM-based format used on disks for the Sega System 24.
 * 
 * Sectors 1-5: 2kB, Sector 6: 1kB, Sector 7: 256 bytes.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

#define sec_no(sec) (((sec) < 5) ? 4 : ((sec) < 6) ? 3 : 1)
#define sec_off(sec) (((sec) < 6) ? (sec)*2048 : 11*1024)

static void *sega_system_24_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int valid_blocks = 0;

    ti->len = 5*2048 + 1024 + 256;
    block = memalloc(ti->len);

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        int idx_off;
        uint8_t dat[2*2048];
        struct ibm_idam idam;

        /* IDAM */
        if (((idx_off = ibm_scan_idam(s, &idam)) < 0) || s->crc16_ccitt)
            continue;

        idam.sec--;
        if (idam.sec >= ti->nr_sectors) {
        unexpected_idam:
            printf("*** T%u: Sega System 24: Bad IDAM %02x:%02x:%02x:%02x\n",
                   tracknr, idam.sec+1, idam.cyl, idam.head, idam.no);
            valid_blocks = 0;
            goto out;
        }

        if ((idam.cyl != (tracknr/2)) || (idam.head != (tracknr&1)) ||
            (idam.no != sec_no(idam.sec)))
            goto unexpected_idam;

        if (valid_blocks & (1u<<idam.sec))
            continue;

        /* DAM */
        if ((ibm_scan_dam(s) < 0) ||
            (stream_next_bytes(s, dat, 2*128<<idam.no) == -1) ||
            (stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
            continue;

        mfm_decode_bytes(MFM_all, 128<<idam.no, dat, dat);
        memcpy(&block[sec_off(idam.sec)], dat, 128<<idam.no);
        valid_blocks |= 1u << idam.sec;
        if (idam.sec == 0)
            ti->data_bitoff = idx_off;
    }

out:
    if (!valid_blocks) {
        memfree(block);
        return NULL;
    }

    ti->data_bitoff = 500;
    ti->valid_sectors = valid_blocks;

    return block;
}

static void sega_system_24_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = tracknr/2, hd = tracknr&1;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        /* IDAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, sec+1);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, sec_no(sec));
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, MFM_all,
                   128<<sec_no(sec), &dat[sec_off(sec)]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 50; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
    }
}

struct track_handler sega_system_24_handler = {
    .density = TRKDEN_mfm_high,
    .bytes_per_sector = 2048,
    .nr_sectors = 7,
    .write_mfm = sega_system_24_write_mfm,
    .read_mfm = sega_system_24_read_mfm
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
