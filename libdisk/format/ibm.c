/*
 * disk/ibm.c
 * 
 * Supported by uPD765A, Intel 8272, and many other FDC chips, as used in
 * pretty much every home computer (except Amiga and C64!).
 * 
 * One of the more useful references:
 *  "uPD765A/7265 Single/Double Density Floppy Disk Controllers",
 *  NEC Electronics Inc.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct ibm_sector {
    struct ibm_idam idam;
    uint8_t mark;
    uint8_t dat[0];
};

struct ibm_track {
    uint8_t has_iam;
    uint8_t gap3;
    struct ibm_sector secs[0];
};

struct ibm_psector {
    struct ibm_psector *next;
    int offset;
    struct ibm_sector s;
};

#define type_is_fm(type) \
    (((type) == TRKTYP_ibm_fm_sd) || ((type) == TRKTYP_ibm_fm_dd))


/***********************************
 * Double-density (IBM-MFM) handlers
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
 */

int ibm_scan_mark(struct stream *s, unsigned int max_scan, uint8_t *pmark)
{
    int idx_off = -1;

    do {
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if ((stream_next_bits(s, 32) == -1) || ((s->word >> 16) != 0x4489))
            break;
        idx_off = s->index_offset_bc - 63;
        if (idx_off < 0)
            idx_off += s->track_len_bc;
        *pmark = (uint8_t)mfm_decode_bits(bc_mfm, s->word);
        break;
    } while ((stream_next_bit(s) != -1) && --max_scan);

    return idx_off;
}

int ibm_scan_idam(struct stream *s, struct ibm_idam *idam)
{
    uint8_t mark;
    int idx_off;

    idx_off = ibm_scan_mark(s, ~0u, &mark);
    if ((idx_off < 0) || (mark != IBM_MARK_IDAM))
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
    uint8_t mark;
    int idx_off = ibm_scan_mark(s, 1000, &mark);
    return (mark == IBM_MARK_DAM) ? idx_off : -1;
}

static int choose_gap3(
    struct track_info *ti, struct ibm_track *ibm_track,
    int gap_bits, unsigned int nr_secs)
{
    int gap3, iam_bits = (type_is_fm(ti->type) ? 7 : 16) * 16;
    int gap4a = 40, gap4b = type_is_fm(ti->type) ? 40 : 80;
    for (;;) {
        /* Don't include gap 4a and 4b (before/after index mark). */
        gap3 = gap_bits - (gap4a + gap4b) * 16;
        /* Account for IAM and distribute gap evenly across sectors and IAM. */
        gap3 = (ibm_track->has_iam
                ? (gap3 - iam_bits) / ((nr_secs+1) * 16)
                : gap3 / (nr_secs * 16));
        if (gap3 >= 25) /* minimum permissible is 24 */
            break;
        /* Inter-sector gap is too small: lengthen the track to make space. */
        gap_bits += 1000;
        ti->total_bits += 1000;
    }
    return gap3;
}

static void *ibm_mfm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_psector *ibm_secs, *new_sec, *cur_sec, *next_sec, **pprev_sec;
    struct ibm_track *ibm_track = NULL;
    unsigned int dat_bytes = 0, gap_bits, nr_blocks = 0;
    unsigned int sec_sz;
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

    ibm_secs = NULL;

    while (stream_next_bit(s) != -1) {

        int idx_off;
        uint8_t mark, dat[2*16384];
        struct ibm_idam idam;

        /* IDAM */
        if (((idx_off = ibm_scan_idam(s, &idam)) < 0) || s->crc16_ccitt)
            continue;

        if (idam.no > 7) {
            trk_warn(ti, tracknr, "Unexpected IDAM no=%02x", idam.no);
            continue;
        }

        sec_sz = 128 << idam.no;

        /* DAM/DDAM */
        if ((ibm_scan_mark(s, 1000, &mark) < 0) ||
            ((mark != IBM_MARK_DAM) && (mark != IBM_MARK_DDAM)) ||
            (stream_next_bytes(s, dat, 2*sec_sz) == -1) ||
            (stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
            continue;

        /* Find correct place for this sector in our linked list of sectors 
         * that we have decoded so far. */
        pprev_sec = &ibm_secs;
        cur_sec = *pprev_sec;
        while (cur_sec && ((idx_off - cur_sec->offset) > 1000)) {
            pprev_sec = &cur_sec->next;
            cur_sec = *pprev_sec;
        }

        /* If this sector's start is within 1000 bits of one we already decoded
         * then it is the same sector: we decoded it already on an earlier 
         * revolution and can skip it this time round. */
        if (cur_sec && (abs(idx_off - cur_sec->offset) < 1000))
            continue;

        mfm_decode_bytes(bc_mfm, sec_sz, dat, dat);

        new_sec = memalloc(sizeof(*new_sec) + sec_sz);
        new_sec->offset = idx_off;
        memcpy(&new_sec->s.dat[0], dat, sec_sz);
        memcpy(&new_sec->s.idam, &idam, sizeof(idam));
        new_sec->s.mark = mark;
        new_sec->next = *pprev_sec;
        *pprev_sec = new_sec;
    }

    gap_bits = ti->total_bits - s->track_len_bc;
    for (cur_sec = ibm_secs; cur_sec; cur_sec = cur_sec->next) {
        int distance, cur_size;
        next_sec = cur_sec->next ?: ibm_secs;
        distance = next_sec->offset - cur_sec->offset;
        if (distance <= 0)
            distance += s->track_len_bc;
        sec_sz = 128 << cur_sec->s.idam.no;
        cur_size = 62 + sec_sz;
        if ((distance -= cur_size * 16) < 0) {
            trk_warn(ti, tracknr, "Overlapping sectors");
            goto out;
        }
        gap_bits += distance;
        nr_blocks++;
        dat_bytes += sec_sz;
    }

    if (nr_blocks == 0)
        goto out;

    ti->data_bitoff = 80 * 16;
    ti->nr_sectors = nr_blocks;
    set_all_sectors_valid(ti);

    ibm_track = memalloc(sizeof(struct ibm_track)
                         + nr_blocks * sizeof(struct ibm_sector)
                         + dat_bytes);

    ibm_track->has_iam = iam ? 1 : 0;
    ibm_track->gap3 = choose_gap3(ti, ibm_track, gap_bits, nr_blocks);

    ti->len = sizeof(struct ibm_track);
    for (cur_sec = ibm_secs; cur_sec; cur_sec = cur_sec->next) {
        sec_sz = 128 << cur_sec->s.idam.no;
        memcpy((char *)ibm_track + ti->len,
               &cur_sec->s, sizeof(struct ibm_sector) + sec_sz);
        ti->len += sizeof(struct ibm_sector) + sec_sz;
    }

out:
    return ibm_track;
}

static void ibm_mfm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_track *ibm_track = (struct ibm_track *)ti->dat;
    struct ibm_sector *cur_sec;
    unsigned int sec, i, sec_sz;

    /* IAM */
    if (ibm_track->has_iam) {
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245224);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245552);
        for (i = 0; i < ibm_track->gap3; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
    }

    cur_sec = ibm_track->secs;
    for (sec = 0; sec < ti->nr_sectors; sec++) {

        sec_sz = 128 << cur_sec->idam.no;

        /* IDAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, IBM_MARK_IDAM);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cur_sec->idam.cyl);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cur_sec->idam.head);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cur_sec->idam.sec);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cur_sec->idam.no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cur_sec->mark);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, sec_sz, cur_sec->dat);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < ibm_track->gap3; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);

        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }

    /* NB. Proper GAP4 should be 0x4e with write splice at index mark. */
}

static void ibm_get_name(
    struct disk *d, unsigned int tracknr, char *str, size_t size)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_track *ibm_track = (struct ibm_track *)ti->dat;
    struct ibm_sector *cur_sec;
    int trk_sz, sec_sz, no, sec;

    cur_sec = ibm_track->secs;
    no = cur_sec->idam.no;
    trk_sz = sec_sz = 128 << no;
    for (sec = 1; sec < ti->nr_sectors; sec++) {
        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
        trk_sz += sec_sz = 128 << cur_sec->idam.no;
        if (no != cur_sec->idam.no)
            no = -1;
    }

    if (no < 0)
        snprintf(str, size, "%s (%u sectors, %u bytes)",
                 ti->typename, ti->nr_sectors, trk_sz);
    else
        snprintf(str, size, "%s (%u %u-byte sectors, %u bytes)",
                 ti->typename, ti->nr_sectors, 128 << no, trk_sz);
}

void setup_ibm_mfm_track(
    struct disk *d, unsigned int tracknr,
    enum track_type type, unsigned int nr_secs, unsigned int no,
    uint8_t *sec_map, uint8_t *cyl_map, uint8_t *head_map,
    uint8_t *mark_map, uint8_t *dat)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_track *ibm_track;
    struct ibm_sector *cur_sec;
    bool_t is_fm = type_is_fm(type);
    unsigned int sec_sz = 128u << no, sec;
    int gap_bits;

    init_track_info(ti, type);

    ti->len = (sizeof(struct ibm_track)
               + nr_secs * (sizeof(struct ibm_sector) + sec_sz));
    ti->dat = memalloc(ti->len);

    ibm_track = (struct ibm_track *)ti->dat;
    cur_sec = ibm_track->secs;
    for (sec = 0; sec < nr_secs; sec++) {
        cur_sec->idam.cyl = cyl_map[sec];
        cur_sec->idam.head = head_map[sec];
        cur_sec->idam.sec = sec_map[sec];
        cur_sec->idam.no = no;
        cur_sec->mark = mark_map[sec];
        memcpy(cur_sec->dat, &dat[sec*sec_sz], sec_sz);
        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }

    ti->total_bits = DEFAULT_BITS_PER_TRACK(d);
    if (handlers[type]->density == trkden_high)
        ti->total_bits *= 2;

    gap_bits = ti->total_bits - nr_secs * ((is_fm ? 33 : 62) + sec_sz) * 16;
    if (gap_bits < 0)
        errx(1, "Too much data for track!");

    ibm_track->has_iam = 1;
    ibm_track->gap3 = choose_gap3(ti, ibm_track, gap_bits, nr_secs);

    ti->data_bitoff = (is_fm ? 40 : 80) * 16;
    ti->nr_sectors = nr_secs;
    set_all_sectors_valid(ti);
}

void retrieve_ibm_mfm_track(
    struct disk *d, unsigned int tracknr,
    uint8_t **psec_map, uint8_t **pcyl_map,
    uint8_t **phead_map, uint8_t **pno_map,
    uint8_t **pmark_map, uint8_t **pdat)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_track *ibm_track = (struct ibm_track *)ti->dat;
    struct ibm_sector *cur_sec;
    uint8_t *sec_map, *cyl_map, *head_map, *no_map, *mark_map, *dat;
    unsigned int sec, sec_sz, dat_sz = 0;

    cur_sec = ibm_track->secs;
    for (sec = 0; sec < ti->nr_sectors; sec++) {
        sec_sz = 128u << cur_sec->idam.no;
        dat_sz += sec_sz;
        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }

    *psec_map = sec_map = memalloc(ti->nr_sectors);
    *pcyl_map = cyl_map = memalloc(ti->nr_sectors);
    *phead_map = head_map = memalloc(ti->nr_sectors);
    *pno_map = no_map = memalloc(ti->nr_sectors);
    *pmark_map = mark_map = memalloc(ti->nr_sectors);
    *pdat = dat = memalloc(dat_sz);

    cur_sec = ibm_track->secs;
    for (sec = 0; sec < ti->nr_sectors; sec++) { 
        sec_sz = 128u << cur_sec->idam.no;
        cyl_map[sec] = cur_sec->idam.cyl;
        head_map[sec] = cur_sec->idam.head;
        sec_map[sec] = cur_sec->idam.sec;
        no_map[sec] = cur_sec->idam.no;
        mark_map[sec] = cur_sec->mark;
        memcpy(dat, cur_sec->dat, sec_sz);
        dat += sec_sz;
        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }
}

struct track_handler ibm_mfm_dd_handler = {
    .density = trkden_double,
    .get_name = ibm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
};

struct track_handler ibm_mfm_hd_handler = {
    .density = trkden_high,
    .get_name = ibm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
};

struct track_handler ibm_mfm_ed_handler = {
    .density = trkden_extra,
    .get_name = ibm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
};


/**********************************
 * Single-density (IBM-FM) handlers
 */

#define IBM_FM_IAM_CLK 0xd7
#define IBM_FM_IAM_RAW 0xf77a

#define IBM_FM_SYNC_CLK 0xc7

#define DEC_RX02_MMFM_DAM_DAT 0xfd
#define DEC_RX02_MMFM_DDAM_DAT 0xf9

static int ibm_fm_scan_mark(
    struct stream *s, unsigned int max_scan, uint8_t *pmark)
{
    int idx_off = -1;

    do {
        if (((s->word>>16) != 0xaaaa) ||
            ((uint8_t)mfm_decode_bits(bc_mfm, s->word>>1) != IBM_FM_SYNC_CLK))
            continue;
        idx_off = s->index_offset_bc - 111;
        if (idx_off < 0)
            idx_off += s->track_len_bc;
        *pmark = (uint8_t)mfm_decode_bits(bc_mfm, s->word);
        stream_start_crc(s);
        s->crc16_ccitt = crc16_ccitt(pmark, 1, 0xffff);
        break;
    } while ((stream_next_bit(s) != -1) && --max_scan);

    return idx_off;
}

static int ibm_fm_scan_idam(struct stream *s, struct ibm_idam *idam)
{
    uint8_t mark;
    int idx_off;

    idx_off = ibm_fm_scan_mark(s, ~0u, &mark);
    if ((idx_off < 0) || (mark != IBM_MARK_IDAM))
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

static void *ibm_fm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_psector *ibm_secs, *new_sec, *cur_sec, *next_sec, **pprev_sec;
    struct ibm_track *ibm_track = NULL;
    unsigned int dat_bytes = 0, gap_bits, nr_blocks = 0;
    unsigned int sec_sz;
    bool_t iam = 0;

    if (ti->type == TRKTYP_dec_rx02)
        stream_set_density(s, 2000u);

    /* IAM */
    while (!iam && (stream_next_bit(s) != -1))
        iam = (s->word == (0xaaaa0000|IBM_FM_IAM_RAW));

    stream_reset(s);

    ibm_secs = NULL;

    while (stream_next_bit(s) != -1) {

        int idx_off;
        uint8_t mark, dat[2*16384];
        struct ibm_idam idam;

        /* IDAM */
        if (((idx_off = ibm_fm_scan_idam(s, &idam)) < 0) || s->crc16_ccitt)
            continue;

        if (idam.no > 7) {
            trk_warn(ti, tracknr, "Unexpected IDAM no=%02x", idam.no);
            continue;
        }

        sec_sz = 128 << idam.no;

        /* DAM/DDAM */
        if (ibm_fm_scan_mark(s, 1000, &mark) < 0)
            continue;
        if ((ti->type == TRKTYP_dec_rx02)
            && ((mark == DEC_RX02_MMFM_DAM_DAT)
                || (mark == DEC_RX02_MMFM_DDAM_DAT))) {
            int i, rc;
            uint16_t crc = s->crc16_ccitt, x = 1;
            idam.no = 1;
            sec_sz = 256;
            stream_set_density(s, 1000u);
            stream_next_bit(s); /* Skip second half of last 2us bitcell? */
            rc = stream_next_bytes(s, dat, 2*(sec_sz+2));
            stream_set_density(s, 2000u);
            if (rc == -1)
                continue;
            /* Undo RX02 modified MFM rule... */
            for (i = 0; i < 2*(sec_sz+2); i++) {
                x = (x << 8) | dat[i];
                if (!(x & 0x1c0)) { dat[i-1] |= 1; x |= 0x40; }
                if (!(x & 0x070)) x |= 0x50;
                if (!(x & 0x01c)) x |= 0x14;
                if (!(x & 0x007)) x |= 0x05;
                dat[i] = x;
            }
            /* ...then extract data bits as usual. */
            mfm_decode_bytes(bc_mfm, sec_sz+2, dat, dat);
            if (crc16_ccitt(dat, sec_sz+2, crc))
                continue;
        } else if ((mark == IBM_MARK_DAM) || (mark == IBM_MARK_DDAM)) {
            if (stream_next_bytes(s, dat, 2*sec_sz) == -1)
                continue;
            if ((stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
                continue;
            mfm_decode_bytes(bc_mfm, sec_sz, dat, dat);
        } else {
            continue;
        }

        /* Find correct place for this sector in our linked list of sectors 
         * that we have decoded so far. */
        pprev_sec = &ibm_secs;
        cur_sec = *pprev_sec;
        while (cur_sec && ((idx_off - cur_sec->offset) > 1000)) {
            pprev_sec = &cur_sec->next;
            cur_sec = *pprev_sec;
        }

        /* If this sector's start is within 1000 bits of one we already decoded
         * then it is the same sector: we decoded it already on an earlier 
         * revolution and can skip it this time round. */
        if (cur_sec && (abs(idx_off - cur_sec->offset) < 1000))
            continue;

        new_sec = memalloc(sizeof(*new_sec) + sec_sz);
        new_sec->offset = idx_off;
        memcpy(&new_sec->s.dat[0], dat, sec_sz);
        memcpy(&new_sec->s.idam, &idam, sizeof(idam));
        new_sec->s.mark = mark;
        new_sec->next = *pprev_sec;
        *pprev_sec = new_sec;
    }

    gap_bits = ti->total_bits - s->track_len_bc;
    for (cur_sec = ibm_secs; cur_sec; cur_sec = cur_sec->next) {
        int distance, cur_size;
        next_sec = cur_sec->next ?: ibm_secs;
        distance = next_sec->offset - cur_sec->offset;
        if (distance <= 0)
            distance += s->track_len_bc;
        sec_sz = 128 << cur_sec->s.idam.no;
        cur_size = 33 + sec_sz;
        if ((distance -= cur_size * 16) < 0) {
            trk_warn(ti, tracknr, "Overlapping sectors");
            goto out;
        }
        gap_bits += distance;
        nr_blocks++;
        dat_bytes += sec_sz;
    }

    if (nr_blocks == 0)
        goto out;

    ti->data_bitoff = 40 * 16;
    ti->nr_sectors = nr_blocks;
    set_all_sectors_valid(ti);

    ibm_track = memalloc(sizeof(struct ibm_track)
                         + nr_blocks * sizeof(struct ibm_sector)
                         + dat_bytes);

    ibm_track->has_iam = iam ? 1 : 0;
    ibm_track->gap3 = ((ti->type == TRKTYP_dec_rx01)
                       || (ti->type == TRKTYP_dec_rx02)) ? 27
        : choose_gap3(ti, ibm_track, gap_bits, nr_blocks);

    ti->len = sizeof(struct ibm_track);
    for (cur_sec = ibm_secs; cur_sec; cur_sec = cur_sec->next) {
        sec_sz = 128 << cur_sec->s.idam.no;
        memcpy((char *)ibm_track + ti->len,
               &cur_sec->s, sizeof(struct ibm_sector) + sec_sz);
        ti->len += sizeof(struct ibm_sector) + sec_sz;
    }

out:
    if (ti->type == TRKTYP_dec_rx02)
        stream_set_density(s, 1000u);
    return ibm_track;
}

#define ENC_RAW      (1u<<0)
#define ENC_HALFRATE (1u<<1)
static void fm_bits(struct tbuf *tbuf, unsigned int flags,
                    unsigned int bits, uint32_t x)
{
    int i;

    for (i = bits-1; i >= 0; i--) {
        uint8_t b = (x >> i) & 1;
        if (!(flags & ENC_RAW) || !(i & 1))
            tbuf->crc16_ccitt = crc16_ccitt_bit(b, tbuf->crc16_ccitt);
        if (!(flags & ENC_RAW)) {
            if (flags & ENC_HALFRATE)
                tbuf->bit(tbuf, SPEED_AVG, bc_raw, 0);
            tbuf->bit(tbuf, SPEED_AVG, bc_raw, 1);
        }
        if (flags & ENC_HALFRATE)
            tbuf->bit(tbuf, SPEED_AVG, bc_raw, 0);
        tbuf->bit(tbuf, SPEED_AVG, bc_raw, b);
    }
}

static uint16_t fm_sync(uint8_t dat, uint8_t clk)
{
    unsigned int i;
    uint16_t sync = 0;
    for (i = 0; i < 8; i++) {
        sync <<= 2;
        sync |= ((clk & 0x80) ? 2 : 0) | ((dat & 0x80) ? 1 : 0);
        clk <<= 1; dat <<= 1;
    }
    return sync;
}

static void ibm_fm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_track *ibm_track = (struct ibm_track *)ti->dat;
    struct ibm_sector *cur_sec;
    unsigned int sec, i, j, sec_sz, flags = 0;

    if (ti->type == TRKTYP_dec_rx02)
        flags |= ENC_HALFRATE;

    /* IAM */
    if (ibm_track->has_iam) {
        for (i = 0; i < 6; i++)
            fm_bits(tbuf, flags, 8, 0x00);
        fm_bits(tbuf, flags|ENC_RAW, 16, IBM_FM_IAM_RAW);
        for (i = 0; i < ibm_track->gap3; i++)
            fm_bits(tbuf, flags, 8, 0xff);
    }

    cur_sec = ibm_track->secs;
    for (sec = 0; sec < ti->nr_sectors; sec++) {

        sec_sz = 128 << cur_sec->idam.no;
        if (ti->type == TRKTYP_dec_rx02)
            cur_sec->idam.no = 0;

        /* IDAM */
        for (i = 0; i < 6; i++)
            fm_bits(tbuf, flags, 8, 0x00);
        tbuf_start_crc(tbuf);
        fm_bits(tbuf, flags|ENC_RAW, 16,
                fm_sync(IBM_MARK_IDAM, IBM_FM_SYNC_CLK));
        fm_bits(tbuf, flags, 8, cur_sec->idam.cyl);
        fm_bits(tbuf, flags, 8, cur_sec->idam.head);
        fm_bits(tbuf, flags, 8, cur_sec->idam.sec);
        fm_bits(tbuf, flags, 8, cur_sec->idam.no);
        fm_bits(tbuf, flags, 16, tbuf->crc16_ccitt);
        for (i = 0; i < 11; i++)
            fm_bits(tbuf, flags, 8, 0xff);

        /* DAM */
        for (i = 0; i < 6; i++)
            fm_bits(tbuf, flags, 8, 0x00);
        tbuf_start_crc(tbuf);
        fm_bits(tbuf, flags|ENC_RAW, 16,
                fm_sync(cur_sec->mark, IBM_FM_SYNC_CLK));
        if ((cur_sec->mark == DEC_RX02_MMFM_DAM_DAT)
            || (cur_sec->mark == DEC_RX02_MMFM_DDAM_DAT)) {
            uint8_t w8, dat[256+2+2];
            uint16_t w16, mmfm[256+2+2], crc;
            uint32_t w32;
            crc = crc16_ccitt(cur_sec->dat, 256, tbuf->crc16_ccitt);
            flags &= ~ENC_HALFRATE;
            fm_bits(tbuf, flags|ENC_RAW, 1, 0); /* 1us of delay to next flux */
            /* MMFM area: Data, CRC, lead-out. */
            memcpy(dat, cur_sec->dat, 256);
            dat[256+0] = crc >> 8; dat[256+1] = crc;
            dat[256+2+0] = dat[256+2+1] = 0xff;
            /* Normal MFM encoding. */
            for (i = w16 = 0; i < sizeof(dat); i++) {
                w8 = dat[i];
                for (j = 0; j < 8; j++) {
                    w16 <<= 2;
                    w16 |= (w8 >> (7-j)) & 1;
                    if (!(w16 & 5))
                        w16 |= 2;
                }
                mmfm[i] = w16;
            }
            /* Apply the extra DEC-RX02 rule: 011110 -> 01000100010. */
            for (i = w32 = 0; i < sizeof(dat); i++) {
                w32 = (w32 << 16) | mmfm[i];
                for (j = 0; j < 16; j += 2) {
                    if ((w32 & (0x555u << (14-j))) == (0x154u << (14-j))) {
                        w32 &= ~(0x7ffu << (14-j));
                        w32 |= 0x222u << (14-j);
                    }
                }
                if (i)
                    mmfm[i-1] = w32 >> 16;
            }
            for (i = 0; i < sizeof(dat); i++)
                fm_bits(tbuf, flags|ENC_RAW, 16, mmfm[i]);
            flags |= ENC_HALFRATE;
        } else {
            for (i = 0; i < sec_sz; i++)
                fm_bits(tbuf, flags, 8, cur_sec->dat[i]);
            fm_bits(tbuf, flags, 16, tbuf->crc16_ccitt);
        }
        for (i = 0; i < ibm_track->gap3; i++)
            fm_bits(tbuf, flags, 8, 0xff);

        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }
}

struct track_handler ibm_fm_sd_handler = {
    .density = trkden_single,
    .get_name = ibm_get_name,
    .write_raw = ibm_fm_write_raw,
    .read_raw = ibm_fm_read_raw
};

struct track_handler ibm_fm_dd_handler = {
    .density = trkden_double,
    .get_name = ibm_get_name,
    .write_raw = ibm_fm_write_raw,
    .read_raw = ibm_fm_read_raw
};

struct track_handler dec_rx01_handler = {
    .density = trkden_double,
    .get_name = ibm_get_name,
    .write_raw = ibm_fm_write_raw,
    .read_raw = ibm_fm_read_raw
};

struct track_handler dec_rx02_handler = {
    .density = trkden_high,
    .get_name = ibm_get_name,
    .write_raw = ibm_fm_write_raw,
    .read_raw = ibm_fm_read_raw
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
