/*
 * disk/ibm_pc.c
 * 
 * 9 (DD) or 18 (HD) 512-byte sectors in IBM System/34 format.
 * 
 * Notes on IBM-compatible MFM data format:
 * ----------------------------------------
 * Supported by uPD765A, Intel 8272, and many other FDC chips, as used in
 * pretty much every home computer (except Amiga and C64!).
 * 
 * One of the more useful references:
 *  "uPD765A/7265 Single/Double Density Floppy Disk Controllers",
 *  NEC Electronics Inc.
 * 
 * Index Address Mark (IAM):
 *      0xc2c2c2fc
 * ID Address Mark (IDAM):
 *      0xa1a1a1fe, <cyl>, <hd> <sec>, <sz>, <crc16_ccitt>
 * Data Address Mark (DAM):
 *      0xa1a1a1fb, <N bytes data>, <crc16_ccitt> [N = 128 << sz]
 * Deleted Data Address Mark (DDAM):
 *      As DAM, but identifier 0xfb -> 0xf8
 * 
 * NB. In above, 0xc2 and 0xa1 are sync marks which have one of their clock
 *     bits forced to zero. Hence 0xc2 -> 0x5224; 0xa1 -> 0x4489.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

int ibm_scan_mark(struct stream *s, uint16_t mark, unsigned int max_scan)
{
    int idx_off = -1;

    do {
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if ((stream_next_bits(s, 32) == -1) || (s->word != (0x44890000|mark)))
            break;
        idx_off = s->index_offset - 63;
        if (idx_off < 0)
            idx_off += s->track_bitlen;
        break;
    } while ((stream_next_bit(s) != -1) && --max_scan);

    return idx_off;
}

int ibm_scan_idam(struct stream *s)
{
    return ibm_scan_mark(s, 0x5554, ~0u);
}

int ibm_scan_dam(struct stream *s)
{
    return ibm_scan_mark(s, 0x5545, 1000);
}

static void *ibm_pc_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len + 1);
    unsigned int valid_blocks = 0;
    bool_t iam = 0;

    /* IAM */
    while (!iam && (stream_next_bit(s) != -1)) {
        if (s->word != 0x52245224)
            continue;
        if (stream_next_bits(s, 32) == -1)
            break;
        iam = (s->word == 0x52245552);
    }

    stream_reset(s);

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        int idx_off;
        unsigned int sz;
        uint8_t dat[2*514], cyl, head, sec, no;

        /* IDAM */
        if ((idx_off = ibm_scan_idam(s)) < 0)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto out;
        cyl = mfm_decode_bits(MFM_all, s->word >> 16);
        head = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        sec = mfm_decode_bits(MFM_all, s->word >> 16);
        no = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        sz = 128 << no;
        if ((cyl != (tracknr/2)) || (head != (tracknr&1)) ||
            (sz != 512) || (s->crc16_ccitt != 0))
            continue;

        sec--;
        if ((sec >= ti->nr_sectors) || (valid_blocks & (1u<<sec)))
            continue;

        /* DAM */
        if (ibm_scan_dam(s) < 0)
            continue;
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto out;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(MFM_all, 512, dat, dat);
        memcpy(&block[sec*512], dat, 512);
        valid_blocks |= 1u << sec;
        if (sec == 0)
            ti->data_bitoff = idx_off;
    }

out:
    if (!valid_blocks) {
        memfree(block);
        return NULL;
    }

    block[ti->len++] = iam;
    ti->data_bitoff = (iam ? 80 : 140) * 16;
    ti->valid_sectors = valid_blocks;

    return block;
}

static void ibm_pc_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = tracknr/2, hd = tracknr&1, no = 2;
    bool_t iam = dat[ti->len-1];
    unsigned int sec, i, gap4;

    gap4 = (ti->type == TRKTYP_ibm_pc_dd) ? 80 : 108;

    /* IAM */
    if (iam) {
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x52245224);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x52245552);
        for (i = 0; i < gap4; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
    }

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
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, MFM_all, 512, &dat[sec*512]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < gap4; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
    }

    /*
     * NB. Proper track gap should be 0x4e recurring up to the index mark.
     * Then write splice. Then ~140*0x4e, leading into 12*0x00.
     */
}

struct track_handler ibm_pc_dd_handler = {
    .density = TRKDEN_mfm_double,
    .bytes_per_sector = 512,
    .nr_sectors = 9,
    .write_mfm = ibm_pc_write_mfm,
    .read_mfm = ibm_pc_read_mfm
};

struct track_handler ibm_pc_hd_handler = {
    .density = TRKDEN_mfm_high,
    .bytes_per_sector = 512,
    .nr_sectors = 18,
    .write_mfm = ibm_pc_write_mfm,
    .read_mfm = ibm_pc_read_mfm
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
