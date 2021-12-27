/*
 * disk/ibm_img.c
 * 
 * 9 (DD), 18 (HD), or 36 (ED) 512-byte sectors in IBM System/34 format.
 * Also support similar Siemens iSDX format with 256-byte sectors.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct ibm_extra_data {
    int sector_base;
};

static void *ibm_img_write_raw(
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

        int sec_sz;
        uint8_t mark, dat[2*16384];
        struct ibm_idam idam;

        /* IDAM */
        if (ibm_scan_idam(s, &idam) < 0)
            continue;

    redo_idam:
        if (s->crc16_ccitt)
            continue;
        /* PCs start numbering sectors at 1, other platforms start at 0. Shift
         * sector number as appropriate.  */
        idam.sec -= extra_data->sector_base;

        if ((idam.sec >= ti->nr_sectors) ||
            (idam.cyl != cyl(tracknr)) ||
            (idam.head != hd(tracknr)) ||
            (idam.no > 7)) {
            trk_warn(ti, tracknr, "Unexpected IDAM sec=%02x cyl=%02x hd=%02x "
                     "no=%02x", idam.sec+extra_data->sector_base,
                     idam.cyl, idam.head, idam.no);
            continue;
        }

        /* Is sector size valid for this format? */
        sec_sz = 128 << idam.no;
        if (sec_sz != ti->bytes_per_sector) {
            trk_warn(ti, tracknr, "Unexpected IDAM sector size sec=%02x "
                     "cyl=%02x hd=%02x secsz=%d wanted=%d",
                     idam.sec+extra_data->sector_base,
                     idam.cyl, idam.head, sec_sz, ti->bytes_per_sector);
            continue;
        }

        if (is_valid_sector(ti, idam.sec))
            continue;

        /* DAM */
        if (ibm_scan_mark(s, 1000, &mark) < 0)
            continue;
        if ((mark == IBM_MARK_IDAM) && (_ibm_scan_idam(s, &idam) == 0))
            goto redo_idam;
        if (mark != IBM_MARK_DAM)
            continue;
        if ((stream_next_bytes(s, dat, 2*sec_sz) == -1) ||
            (stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
            continue;

        mfm_decode_bytes(bc_mfm, sec_sz, dat, dat);
        memcpy(&block[idam.sec*sec_sz], dat, sec_sz);
        set_sector_valid(ti, idam.sec);
        nr_valid_blocks++;
    }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    block[ti->len++] = iam;
    ti->data_bitoff = 80*16; /* Gap 4A */

    return block;
}

static void ibm_img_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_extra_data *extra_data = handlers[ti->type]->extra_data;
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = cyl(tracknr), hd = hd(tracknr), no;
    bool_t iam = dat[ti->len-1];
    unsigned int sec, i, gap3;

    tbuf_set_gap_fill_byte(tbuf, 0x4e);

    for (no = 0; (128<<no) != ti->bytes_per_sector; no++)
        continue;

    gap3 = ((ti->type == TRKTYP_ibm_pc_dd) ? 84
            : (ti->type == TRKTYP_atari_st_720kb) ? 84
            : (ti->type == TRKTYP_ibm_pc_dd_10sec) ? 30
            : 108);

    /* Gap 4A is included in data start offset */

    /* IAM */
    if (iam) {
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245224);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245552);
        for (i = 0; i < 50; i++)
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
        for (i = 0; i < gap3; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
    }
}

void *ibm_img_write_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    bool_t iam = 1;

    if (ti->type == TRKTYP_atari_st_720kb)
        iam = 0;

    if (sectors->nr_bytes < ti->len)
        return NULL;

    block = memalloc(ti->len + 1);
    memcpy(block, sectors->data, ti->len);

    sectors->data += ti->len;
    sectors->nr_bytes -= ti->len;

    block[ti->len++] = iam;
    ti->data_bitoff = 80*16; /* Gap 4A */

    return block;
}

void ibm_img_read_sectors(
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
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Non-standard 10-sector version of the above, with reduced sector gap. */
struct track_handler ibm_pc_dd_10sec_handler = {
    .density = trkden_double,
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* IBM PC 5.25 HD 1200K */
struct track_handler ibm_pc_hd_5_25_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 15,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

struct track_handler ibm_pc_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 18,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

struct track_handler ibm_pc_ed_handler = {
    .density = trkden_extra,
    .bytes_per_sector = 512,
    .nr_sectors = 36,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Siemens iSDX telephone exchange. 80 tracks. */
struct track_handler siemens_isdx_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 256,
    .nr_sectors = 32,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Microsoft DMF, High Density format
 * 21 spt, 512 bytes/sector, 80 tracks */
struct track_handler microsoft_dmf_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 21,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Trace Mountain Products / Magnetic Design Corp "TRACEBACK" duplicator info
 * 1 spt, 2048 bytes/sector, 1 track
 * Always stored on phys cyl 80, heads 0 & 1, identical data on both sides. */
struct track_handler trace_traceback_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 2048,
    .nr_sectors = 1,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
};

/* Acorn ADFS "Small", "Medium" and "Large"
 *   S is 40 tracks, single sided, DD
 *   M is 50 tracks, double sided, DD
 *   L is 80 tracks, double sided, DD */
struct track_handler acorn_adfs_s_m_l_handler = {
    .density = trkden_double,
    .bytes_per_sector = 256,
    .nr_sectors = 16,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/* Acorn ADFS "D" or "E" - 80tk double sided DD */
struct track_handler acorn_adfs_d_e_handler = {
    .density = trkden_double,
    .bytes_per_sector = 1024,
    .nr_sectors = 5,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/* Acorn ADFS "F" - 80tk double sided HD */
struct track_handler acorn_adfs_f_handler = {
    .density = trkden_high,
    .bytes_per_sector = 1024,
    .nr_sectors = 10,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 0
    }
};

/* There are also two Acorn DFS formats from the BBC Micro which require
 * FM decode support:
 *   DFS 40-track - 40tk DS 10/256  200K  FM/SD
 *   DFS 80-track - 80th DS 10/256  400K  FM/SD */

struct track_handler atari_st_720kb_handler = {
    .density = trkden_double,
    .bytes_per_sector = 512,
    .nr_sectors = 9,
    .write_raw = ibm_img_write_raw,
    .read_raw = ibm_img_read_raw,
    .write_sectors = ibm_img_write_sectors,
    .read_sectors = ibm_img_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .sector_base = 1
    }
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
