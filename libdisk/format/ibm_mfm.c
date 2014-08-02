/*
 * disk/ibm_mfm.c
 * 
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

struct ibm_sector {
    struct ibm_idam idam;
    uint8_t mark;
    uint8_t dat[0];
};

struct ibm_track {
    uint8_t has_iam;
    uint8_t gap4;
    struct ibm_sector secs[0];
};

struct ibm_psector {
    struct ibm_psector *next;
    int offset;
    struct ibm_sector s;
};

int ibm_scan_mark(struct stream *s, unsigned int max_scan, uint8_t *pmark)
{
    int idx_off = -1;

    do {
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if ((stream_next_bits(s, 32) == -1) || ((s->word >> 16) != 0x4489))
            break;
        idx_off = s->index_offset - 63;
        if (idx_off < 0)
            idx_off += s->track_bitlen;
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

static int choose_gap4(
    struct ibm_track *ibm_track, unsigned int gap_bits, unsigned int nr_secs)
{
    int gap4 = ibm_track->has_iam
        ? (gap_bits - 16*16) / ((nr_secs+1) * 16)
        : gap_bits / (nr_secs * 16);
    gap4 = (gap4 > 108+2) ? 108
        : (gap4 > 80+2) ? 80
        : (gap4 > 40+2) ? 40
        : 20;
    return gap4;
}

static void *ibm_mfm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ibm_psector *ibm_secs, *new_sec, *cur_sec, *next_sec, **pprev_sec;
    struct ibm_track *ibm_track = NULL;
    unsigned int dat_bytes = 0, total_distance = 0, nr_blocks = 0;
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

        /* DAM/DDAM */
        if (((idx_off = ibm_scan_mark(s, 1000, &mark)) < 0) ||
            ((mark != IBM_MARK_DAM) && (mark != IBM_MARK_DDAM)) ||
            (stream_next_bytes(s, dat, 2*sec_sz) == -1) ||
            (stream_next_bits(s, 32) == -1) || s->crc16_ccitt)
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

    for (cur_sec = ibm_secs; cur_sec; cur_sec = cur_sec->next) {
        int distance, cur_size;
        next_sec = cur_sec->next ?: ibm_secs;
        distance = next_sec->offset - cur_sec->offset;
        if (distance <= 0)
            distance += s->track_bitlen;
        sec_sz = 128 << cur_sec->s.idam.no;
        cur_size = 62 + sec_sz;
        if ((distance -= cur_size * 16) < 0) {
            trk_warn(ti, tracknr, "Overlapping sectors");
            goto out;
        }
        total_distance += distance;
        nr_blocks++;
        dat_bytes += sec_sz;
    }

    if (nr_blocks == 0)
        goto out;

    ti->data_bitoff = (iam ? 80 : 140) * 16;
    ti->nr_sectors = nr_blocks;
    set_all_sectors_valid(ti);

    ibm_track = memalloc(sizeof(struct ibm_track)
                         + nr_blocks * sizeof(struct ibm_sector)
                         + dat_bytes);

    ibm_track->has_iam = iam ? 1 : 0;
    ibm_track->gap4 = choose_gap4(ibm_track, total_distance, nr_blocks);

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
        for (i = 0; i < ibm_track->gap4; i++)
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
        for (i = 0; i < ibm_track->gap4; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);

        cur_sec = (struct ibm_sector *)
            ((char *)cur_sec + sizeof(struct ibm_sector) + sec_sz);
    }

    /* NB. Proper track gap should be 0x4e recurring up to the index mark.
     * Then write splice. Then ~140*0x4e, leading into 12*0x00. */
}

static void ibm_mfm_get_name(
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

    ti->total_bits = DEFAULT_BITS_PER_TRACK;
    if (handlers[type]->density == trkden_high)
        ti->total_bits *= 2;

    gap_bits = ti->total_bits - nr_secs * (62 + sec_sz) * 16;
    if (gap_bits < 0)
        errx(1, "Too much data for track!");

    ibm_track->has_iam = 1;
    ibm_track->gap4 = choose_gap4(ibm_track, gap_bits, nr_secs);

    ti->data_bitoff = (ibm_track->has_iam ? 80 : 140) * 16;
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
    .get_name = ibm_mfm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
};

struct track_handler ibm_mfm_hd_handler = {
    .density = trkden_high,
    .get_name = ibm_mfm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
};

struct track_handler ibm_mfm_ed_handler = {
    .density = trkden_extra,
    .get_name = ibm_mfm_get_name,
    .write_raw = ibm_mfm_write_raw,
    .read_raw = ibm_mfm_read_raw
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
