/*
 * disk/ibm_pc.c
 * 
 * 9 (DD), 18 (HD), or 36 (ED) 512-byte sectors in IBM System/34 format.
 * Also support similar Siemens iSDX format with 256-byte sectors.
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
#include "../private.h"

struct ibm_extra_data {
    int sector_base;
};

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

int ibm_scan_idam(struct stream *s, struct ibm_idam *idam)
{
    int idx_off = ibm_scan_mark(s, 0x5554, ~0u);
    if (idx_off < 0)
        goto fail;

    /* cyl,head */
    if (stream_next_bits(s, 32) == -1)
        goto fail;
    idam->cyl = mfm_decode_bits(bc_mfm, s->word >> 16);
    idam->head = mfm_decode_bits(bc_mfm, s->word);

    /* sec,no */
    if (stream_next_bits(s, 32) == -1)
        goto fail;
    idam->sec = mfm_decode_bits(bc_mfm, s->word >> 16);
    idam->no = mfm_decode_bits(bc_mfm, s->word);

    /* crc */
    if (stream_next_bits(s, 32) == -1)
        goto fail;

    return idx_off;
fail:
    return -1;
}

int ibm_scan_dam(struct stream *s)
{
    return ibm_scan_mark(s, 0x5545, 1000);
}

static void *ibm_pc_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_extra_data *extra_data = handlers[ti->type]->extra_data;
    char *block = memalloc(ti->len + 1);
    unsigned int nr_valid_blocks = 0;
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
           (nr_valid_blocks != ti->nr_sectors)) {

        int idx_off, sec_sz;
        uint8_t dat[2*16384];
        struct ibm_idam idam;

        /* IDAM */
        if (((idx_off = ibm_scan_idam(s, &idam)) < 0) || s->crc16_ccitt)
            continue;

        /* PCs start numbering sectors at 1, other platforms start at 0. Shift sector number as appropriate. */
        idam.sec -= extra_data->sector_base;

        if ((idam.sec >= ti->nr_sectors) ||
            (idam.cyl != (tracknr/2)) ||
            (idam.head != (tracknr&1)) ||
            (idam.no > 7)) {
            trk_warn(ti, tracknr, "Unexpected IDAM sec=%02x cyl=%02x hd=%02x "
                     "no=%02x", idam.sec+extra_data->sector_base, idam.cyl, idam.head, idam.no);
            continue;
        }

        /* Is sector size valid for this format? */
        sec_sz = 128 << idam.no;
        if (sec_sz != ti->bytes_per_sector) {
            trk_warn(ti, tracknr, "Unexpected IDAM sector size sec=%02x "
                     "cyl=%02x hd=%02x secsz=%d wanted=%d", idam.sec+extra_data->sector_base,
                     idam.cyl, idam.head, sec_sz, ti->bytes_per_sector);
            continue;
        }

        if (is_valid_sector(ti, idam.sec))
            continue;

        /* DAM */
        if ((ibm_scan_dam(s) < 0) ||
            (stream_next_bytes(s, dat, 2*sec_sz) == -1) ||
            (stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
            continue;

        mfm_decode_bytes(bc_mfm, sec_sz, dat, dat);
        memcpy(&block[idam.sec*sec_sz], dat, sec_sz);
        set_sector_valid(ti, idam.sec);
        nr_valid_blocks++;
        if (idam.sec == 0)
            ti->data_bitoff = idx_off;
    }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    block[ti->len++] = iam;
    ti->data_bitoff = (iam ? 80 : 140) * 16;

    return block;
}

static void ibm_pc_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_extra_data *extra_data = handlers[ti->type]->extra_data;
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = tracknr/2, hd = tracknr&1, no;
    bool_t iam = dat[ti->len-1];
    unsigned int sec, i, gap4;

    for (no = 0; (128<<no) != ti->bytes_per_sector; no++)
        continue;

    gap4 = (ti->type == TRKTYP_ibm_pc_dd) ? 80 : 108;

    /* IAM */
    if (iam) {
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245224);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245552);
        for (i = 0; i < gap4; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
    }

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        /* IDAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sec+extra_data->sector_base);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, ti->bytes_per_sector,
                   &dat[sec*ti->bytes_per_sector]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < gap4; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
    }

    /*
     * NB. Proper track gap should be 0x4e recurring up to the index mark.
     * Then write splice. Then ~140*0x4e, leading into 12*0x00.
     */
}

void *ibm_pc_write_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    bool_t iam = 1;

    if (sectors->nr_bytes < ti->len)
        return NULL;

    block = memalloc(ti->len + 1);
    memcpy(block, sectors->data, ti->len);

    sectors->data += ti->len;
    sectors->nr_bytes -= ti->len;

    block[ti->len++] = iam;
    ti->data_bitoff = (iam ? 80 : 140) * 16;

    return block;
}

void ibm_pc_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];

    sectors->nr_bytes = ti->len - 1;
    sectors->data = memalloc(sectors->nr_bytes);
    memcpy(sectors->data, ti->dat, sectors->nr_bytes);
}


/* IBM PC 3.5 720K (80 track) and 5.25in 360K (40 track) */
struct track_handler ibm_pc_dd_handler = {
    .density = trkden_double,
    .bytes_per_sector = 512,
    .nr_sectors = 9,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* IBM PC 5.25 HD 1200K */
struct track_handler ibm_pc_hd_5_25_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 15,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

struct track_handler ibm_pc_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 18,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

struct track_handler ibm_pc_ed_handler = {
    .density = trkden_extra,
    .bytes_per_sector = 512,
    .nr_sectors = 36,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Siemens iSDX telephone exchange. 80 tracks. */
struct track_handler siemens_isdx_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 256,
    .nr_sectors = 32,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/*
 * Microsoft DMF, High Density format
 * 21 spt, 512 bytes/sector, 80 tracks
 */
struct track_handler microsoft_dmf_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 21,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/*
 * Trace Mountain Products / Magnetic Design Corp "TRACEBACK" duplicator info
 * 1 spt, 2048 bytes/sector, 1 track
 * Always stored on phys cyl 80, heads 0 and 1, identical data on both sides.
 */
struct track_handler trace_traceback_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 2048,
    .nr_sectors = 1,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/*
 * Acorn ADFS "Small", "Medium" and "Large"
 *   S is 40 tracks, single sided, DD
 *   M is 50 tracks, double sided, DD
 *   L is 80 tracks, double sided, DD
 */
struct track_handler acorn_adfs_s_m_l_handler = {
    .density = trkden_double,
    .bytes_per_sector = 256,
    .nr_sectors = 16,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/* Acorn ADFS "D" or "E" - 80tk double sided DD */
struct track_handler acorn_adfs_d_e_handler = {
    .density = trkden_double,
    .bytes_per_sector = 1024,
    .nr_sectors = 5,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/* Acorn ADFS "F" - 80tk double sided HD */
struct track_handler acorn_adfs_f_handler = {
    .density = trkden_high,
    .bytes_per_sector = 1024,
    .nr_sectors = 10,
    .write_raw = ibm_pc_write_raw,
    .read_raw = ibm_pc_read_raw,
    .write_sectors = ibm_pc_write_sectors,
    .read_sectors = ibm_pc_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/*
 * There are also two Acorn DFS formats from the BBC Micro which require
 * FM decode support:
 *   DFS 40-track - 40tk DS 10/256  200K  FM/SD
 *   DFS 80-track - 80th DS 10/256  400K  FM/SD
 */




/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
