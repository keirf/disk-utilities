/*
 * softlock_dualformat.c
 *
 * SoftLock Dual Format track as used on boot track for Amiga/PC disks.
 * Specifically seen only on Ace Issue 55 (April 1992). All other tracks are
 * either solely AmigaDOS or PC (9 sectors). The boot track must be readable by
 * all systems: Amiga requires a valid bootblock, and PC encodes the FAT root
 * block in track 0.
 *
 * Written in 2020 by Keir Fraser
 *
 * Raw track layout is 11 AmigaDOS sectors, with the usual 00-bytes track gap.
 * 
 * However, three sectors have an IBM IDAM hidden within (IBM sector IDs 1, 2, 
 * and 8), and three sectors have the corresponding IBM DAMs. The sector data
 * starts immediately in the AmigaDOS data area, and ends with the CRC
 * in the label of the next sector.
 * 
 * Essentially this is a simplified form of RNC Tri-Format, as no AmigaDOS
 * sector contains both an IDAM and a DAM. It is also more restrictive, as
 * only three IBM sectors are included, rather than nine. Perhaps this is
 * why Ace Issue 55 had to omit ST support at the last moment!
 */

#include <libdisk/util.h>
#include <private/disk.h>

#if 0
#define INFO(f, a...) fprintf(stderr, f, ##a)
#else
#define INFO(f, a...) ((void)0)
#endif

struct ados_hdr {
    uint8_t  format, track, sector, sectors_to_gap;
    uint8_t  lbl[16];
    uint32_t hdr_checksum;
    uint32_t dat_checksum;
};

struct tri_data {
    /* Five AmigaDOS sectors are free to contain arbitrary data. */
    uint8_t ados_sec[5][512]; /* 0,1,8,9,10 */
    /* Three IBM sectors include next AmigaDOS sector's header (8 bytes). We
     * don't need to save these as we re-generate them.*/
    uint8_t ibm_sec[3][512-8];
};

const static uint8_t ibm_id[] = { 1, 2, 8 };

#define SEC_ados 0
#define SEC_idam 1
#define SEC_dam  2
/* ados, idam, dam, idam, dam, idam, dam, ados, ados, ados, ados */
const static uint32_t sec_types = 0x2664;

static int mem_check_pattern(const void *p, uint8_t c, size_t nr)
{
    const uint8_t *q = p;
    while (nr--)
        if (*q++ != c)
            return -1;
    return 0;
}

static void *softlock_dualformat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct tri_data *td;
    unsigned int sec, ibm_sec;
    uint32_t idx_off;
    struct ados_hdr ados_hdr;
    uint8_t dat[4+4+512+2], raw[2*(sizeof(struct ados_hdr)+512+2)];
    uint8_t gap[2];

    td = memalloc(sizeof(*td));

retry:

    while ((stream_next_bit(s) != -1) && (s->word != 0x44894489))
        continue;

    idx_off = s->index_offset_bc - 31;

    /* Fix up loop entry. */
    if (stream_next_bytes(s, raw, 2*6) == -1)
        goto fail;
    s->word = 0x44894489;

    ibm_sec = 0;
    for (sec = 0; sec < 11; sec++) {

        unsigned int ados_id = (sec + 1) % 11;
        unsigned int sec_type = (sec_types >> (sec<<1)) & 3;

        if ((s->word != 0x44894489)
            || (stream_next_bytes(s, &raw[2*6], sizeof(raw)-2*6) == -1)) {
            INFO("Fail %u sync\n", sec);
            goto fail;
        }

        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2*0], &ados_hdr);
        mfm_decode_bytes(bc_mfm_even_odd, 16, &raw[2*4], ados_hdr.lbl);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2*20],
                         &ados_hdr.hdr_checksum);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2*24],
                         &ados_hdr.dat_checksum);
        mfm_decode_bytes(bc_mfm_even_odd, 512, &raw[2*28], dat);
        mfm_decode_bytes(bc_mfm_even_odd, 2, &raw[2*(28+512)], gap);

        /* Sanity-check the AmigaDOS sector header. */
        ados_hdr.hdr_checksum = be32toh(ados_hdr.hdr_checksum);
        ados_hdr.dat_checksum = be32toh(ados_hdr.dat_checksum);
        if ((amigados_checksum(&ados_hdr, 20) != ados_hdr.hdr_checksum) ||
            (amigados_checksum(dat, 512) != ados_hdr.dat_checksum) ||
            (ados_hdr.sector != ados_id) || (ados_hdr.format != 0xffu) ||
            (ados_hdr.track != tracknr) ||
            (ados_hdr.sectors_to_gap != (11-sec))) {
            INFO("Fail %u header\n", sec);
            goto fail;
        }

        /* AmigaDOS inter-sector gaps must all be zero. */
        if ((gap[0]|gap[1]) != 0) {
            INFO("Fail %u gap\n", sec);
            goto fail;
        }

        /* Sanity-check the header label area. */
        switch (sec) {
        case 3: case 5: case 7:
            /* First two label bytes are IBM DAM CRC. Remainder are 00 gap. */
            mfm_decode_bytes(bc_mfm, 16, &raw[2*4], ados_hdr.lbl);
            if (mem_check_pattern(&ados_hdr.lbl[2], 0x00, 16-2)) {
                INFO("Fail %u label\n", sec);
                goto fail;
            }
            break;
        default:
            /* Empty label area in sectors which don't follow a DAM. */
            if (mem_check_pattern(ados_hdr.lbl, 0x00, 16)) {
                INFO("Fail %u label\n", sec);
                goto fail;
            }
            break;
        }

        /* Five AmigaDOS sectors contain arbitrary data. Save it. */
        if (sec_type == SEC_ados) {
            int off = (ados_id <= 1) ? ados_id : ados_id - 6;
            memcpy(td->ados_sec[off], dat, 512);
        }

        /* Now construct the IBM DAM (and the next IDAM enclosed within it). 
         * The DAM starts immediately in the AmigaDOS data area:
         * 4*00, 3*A1, FB, ... */
        memmove(raw, &raw[2*28], sizeof(raw)-2*28);
        if (stream_next_bytes(s, &raw[sizeof(raw)-2*28], 2*8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 4+4+512+2, raw, dat);

        switch (sec_type) {

        /* Only three AmigaDOS sectors contain an IDAM. */
        case SEC_idam:
            /* Check start of sector is empty, followed by 50x4E leading 
             * into the IDAM. */
            if (mem_check_pattern(dat, 0x00, 440)
                || mem_check_pattern(dat+440, 0x4e, 50))
                goto fail;
            /* Shift the IDAM to the start of the dat[] array. */
            memmove(dat, dat+4+4+482, 12+4+4+2);
            /* MFM IDAM: 12*00, 3*A1, FE, C, H, R, N, CRC */
            if (mem_check_pattern(dat, 0x00, 12) ||
                mem_check_pattern(dat+12, 0xa1, 3) ||
                /* ID = IDAM (FE) */
                (dat[15] != 0xfe) ||
                /* C, H = track */
                (dat[16] != (tracknr>>1)) || (dat[17] != (tracknr&1)) ||
                /* R = correct sector id, N = 2 (512 bytes) */
                (dat[18] != ibm_id[ibm_sec]) || (dat[19] != 2) ||
                crc16_ccitt(dat+12, 4+4+2, 0xffff)) {
                INFO("Fail %u idam\n", sec);
                goto fail;
            }
            INFO("IDAM CRC %04x\n", be16toh(*(uint16_t *)(dat+20)));
            break;

        /* Only three AmigaDOS sectors contain a DAM. */
        case SEC_dam:
            /* MFM DAM: 4*00, 3*A1, FB, <data>, CRC */
            if (mem_check_pattern(dat, 0x00, 4) ||
                mem_check_pattern(dat+4, 0xa1, 3) ||
                /* ID = DAM (FB) */
                (dat[7] != 0xfb) ||
                crc16_ccitt(dat+4, 4+512+2, 0xffff)) {
                INFO("Fail %u dam\n", sec);
                goto fail;
            }
            INFO(" DAM CRC %04x\n", be16toh(*(uint16_t *)(dat+520)));
            /* Save the IBM data. */
            memcpy(td->ibm_sec[ibm_sec++], dat + 4 + 4, 512-8);
            break;

        }

        /* Shift next AmigaDOS sector header to start of the dat[] array. */
        s->word = be32toh(*(uint32_t *)&raw[sizeof(raw)-2*28]);
        memmove(raw, &raw[sizeof(raw)-2*26], 2*6);
    }

    set_all_sectors_valid(ti);
    ti->data_bitoff = idx_off - 32; /* allow for pre-sync gap */
    ti->len = sizeof(*td);
    return td;

fail:
    stream_next_index(s);
    if (stream_next_bit(s) != -1) {
        INFO("Retry...\n");
        goto retry;
    }
    memfree(td);
    return NULL;
}

static void sync_fixup(void *p, int nr)
{
    uint16_t *sync = p;
    while (nr--) {
        BUG_ON(be16toh(*sync) != 0x44a9);
        *sync++ = htobe16(0x4489);
    }
}

static void softlock_dualformat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ados_hdr ados_hdr;
    struct tri_data *td = (struct tri_data *)ti->dat;
    uint8_t buf[544*11+10], *p, *q;
    uint8_t raw[2*(544*11+10)];
    uint8_t ados_dat[1024];
    unsigned int sec, ibm_sec;
    uint32_t csum;

#define ados_to_ibm(_src, _dst, _nr)                        \
    mfm_encode_bytes(bc_mfm_even_odd, _nr, _src, raw, 0);   \
    mfm_decode_bytes(bc_mfm, _nr, raw, _dst);
#define ibm_to_ados(_src, _dst, _nr)                        \
    mfm_encode_bytes(bc_mfm, _nr, _src, raw, 0);            \
    mfm_decode_bytes(bc_mfm_even_odd, _nr, raw, _dst);

    p = buf;

    ibm_sec = 0;
    for (sec = 0; sec < 11; sec++) {

        unsigned int ados_id = (sec + 1) % 11;
        unsigned int sec_type = (sec_types >> (sec<<1)) & 3;

        memset(p, 0x00, 2); p += 2;
        memset(p, 0xa1, 2); p += 2;
        memset(&ados_hdr, 0, sizeof(ados_hdr));
        ados_hdr.format = 0xff;
        ados_hdr.track = tracknr;
        ados_hdr.sector = ados_id;
        ados_hdr.sectors_to_gap = 11-sec;
        ados_to_ibm(&ados_hdr, p, 4); p += 4;

        if ((sec == 3) || (sec == 5) || (sec == 7)) {
            /* This sector label area follows a DAM, and contains the CRC. */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-516, 516, 0xffff)); p += 2;
            memset(p, 0x00, 14); p += 14;
        } else {
            memset(p, 0x00, 16); p += 16;
        }

        /* AmigaDOS header checksum. */
        /* 1. Convert the label area into Amiga even+odd bit order. */
        ibm_to_ados(p-16, ados_hdr.lbl, 16);
        /* 2. Compute the checksum. */
        csum = htobe32(amigados_checksum(&ados_hdr, 20));
        /* 3. Convert the checksum into IBM MFM sequential bit order. */
        ados_to_ibm(&csum, p, 4); p += 4;

        /* Skip AmigaDOS data checksum for now. */
        p += 4;

        switch (sec_type) {

        case SEC_ados: {
            /* AmigaDOS sector data */
            int off = (ados_id <= 1) ? ados_id : ados_id - 6;
            memcpy(ados_dat, td->ados_sec[off], 512);
            ados_to_ibm(ados_dat, p, 512);
            p += 512;
            break;
        }

        case SEC_idam:
            /* IDAM sector */
            memset(p, 0x00, 440); p += 440;
            memset(p, 0x4e, 50); p += 50;
            memset(p, 0x00, 12); p += 12;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfe;
            *p++ = tracknr >> 1;    /* C */
            *p++ = tracknr & 1;     /* H */
            *p++ = ibm_id[ibm_sec]; /* R */
            *p++ = 2;               /* N */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-8, 8, 0xffff)); /* CRC */
            p += 2;
            break;

        case SEC_dam:
            /* DAM sector */
            memset(p, 0x00, 4); p += 4;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfb;
            memcpy(p, td->ibm_sec[ibm_sec++], 512-8);
            p += 512-8;
            break;

        }

    }

    /* AmigaDOS data checksums. */
    q = p;
    p = buf;
    for (sec = 0; sec < 11; sec++) {
        ibm_to_ados(p+32, ados_dat, 512);
        csum = htobe32(amigados_checksum(ados_dat, 512));
        ados_to_ibm(&csum, p+28, 4);
        p += 544;
    }

    mfm_encode_bytes(bc_mfm, q-buf, buf, raw, 0);

    /* Fix up 4489 sync words. */
    p = raw;
    for (sec = 0; sec < 11; sec++) {
        /* AmigaDOS sync */
        sync_fixup(p+4, 2); /* AmigaDOS sync */
        if ((sec == 1) || (sec == 3) || (sec == 5))
            sync_fixup(p+2*(544-10), 3); /* IDAM sync */
        if ((sec == 2) || (sec == 4) || (sec == 6))
            sync_fixup(p+2*36, 3); /* DAM sync */
        p += 544*2;
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_raw, 2*(q-buf), raw);
}

static void softlock_dualformat_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct tri_data *td = (struct tri_data *)ti->dat;
    unsigned int i;
    char *p;

    sectors->nr_bytes = 9*512;
    sectors->data = memalloc(sectors->nr_bytes);

    p = (char *)sectors->data;
    for (i = 0; i < 3; i++)
        memcpy(p+(ibm_id[i]-1)*512, td->ibm_sec[i], 512-8);
}

void *softlock_dualformat_to_ados(struct disk *d, unsigned int tracknr)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct tri_data *td = (struct tri_data *)ti->dat;
    char *p = memalloc(11*512);

    memcpy(p+ 0*512, td->ados_sec[0], 512);
    memcpy(p+ 1*512, td->ados_sec[1], 512);
    memcpy(p+ 8*512, td->ados_sec[2], 512);
    memcpy(p+ 9*512, td->ados_sec[3], 512);
    memcpy(p+10*512, td->ados_sec[4], 512);

    return p;
}

struct track_handler softlock_dualformat_handler = {
    .write_raw = softlock_dualformat_write_raw,
    .read_raw = softlock_dualformat_read_raw,
    .read_sectors = softlock_dualformat_read_sectors
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
