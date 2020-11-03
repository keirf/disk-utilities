/*
 * rnc_triformat.c
 *
 * Rob Northen Tri-Format track as used on boot track for Amiga/ST/PC disks.
 * All other tracks are either solely AmigaDOS or PC (9 sectors). PC and ST
 * both share the FAT filesystem encoded on the PC tracks.
 * The boot track must be readable by all systems: Amiga requires a valid
 * bootblock, and PC/ST encode the FAT root block in track 0.
 *
 * Written in 2020 by Keir Fraser
 *
 * Raw track layout is 11 AmigaDOS sectors, with the usual 00-bytes track gap.
 * 
 * However, all sectors after the bootblock (sectors 2-10) have an IBM/PC
 * sector hidden within. The sector data starts immediately in the AmigaDOS
 * data area, and ends with the CRC and GAP3 in the label of the next sector.
 * 
 * Moreover, the IDAM for each PC sector resides in the *final* bytes of the
 * preceding AmigaDOS sector data area. Hence each PC DAM (except the last)
 * contains the IDAM for the next sector.
 * 
 * Checksum computation: IDAMs first, then DAMs, then AmigaDOS checksums.
 * Fortunately (or by design) the AmigaDOS checksums do not reside within
 * any PC IDAM or DAM region.
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define STD_SEC 512

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
    /* Only first section of Amiga bootblock is non-zero. Conservatively save 
     * 256 bytes to cover that region. Exclude the first 12 bytes which we 
     * re-generate (bootblock signature, flags, root block, checksum). */
    uint8_t ami_bb[256-12];
    /* First PC sector contains full 512 bytes free data. */
    uint8_t pc_sec1[512];
    /* All other PC sectors include the next PC sector's IDAM (22 bytes), and
     * the next AmigaDOS sector's header (8 bytes). We don't need to save these
     * as we re-generate them.*/
    uint8_t pc_secN[8][512-8-22];
};

const static uint8_t ibm_secs[] = { 6, 2, 7, 3, 8, 4, 9, 5, 1 };

static int mem_check_pattern(const void *p, uint8_t c, size_t nr)
{
    const uint8_t *q = p;
    while (nr--)
        if (*q++ != c)
            return -1;
    return 0;
}

static uint32_t amiga_bootblock_checksum(void *dat)
{
    uint32_t csum = 0, *bb = dat;
    unsigned int i;
    for (i = 0; i < 1024/4; i++) {
        uint32_t x = be32toh(bb[i]);
        if ((csum + x) < csum)
            csum++;
        csum += x;
    }
    return ~csum;
}

static void *rnc_triformat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *amiga_block, *pc_block;
    struct tri_data *td;
    unsigned int sec;
    uint32_t idx_off;
    struct ados_hdr ados_hdr;
    uint8_t dat[4+4+512+2], raw[2*(sizeof(struct ados_hdr)+STD_SEC+2)];
    uint8_t gap[2];

    amiga_block = memalloc(512 * 11);
    pc_block = memalloc(512 * 9);

retry:

    while ((stream_next_bit(s) != -1) && (s->word != 0x44894489))
        continue;

    idx_off = s->index_offset_bc - 31;

    /* Fix up loop entry. */
    if (stream_next_bytes(s, raw, 2*6) == -1)
        goto fail;
    s->word = 0x44894489;

    for (sec = 0; sec < 11; sec++) {

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
        mfm_decode_bytes(bc_mfm_even_odd, STD_SEC, &raw[2*28], dat);
        mfm_decode_bytes(bc_mfm_even_odd, 2, &raw[2*(28+STD_SEC)], gap);

        /* Sanity-check the AmigaDOS sector header. */
        ados_hdr.hdr_checksum = be32toh(ados_hdr.hdr_checksum);
        ados_hdr.dat_checksum = be32toh(ados_hdr.dat_checksum);
        if ((amigados_checksum(&ados_hdr, 20) != ados_hdr.hdr_checksum) ||
            (amigados_checksum(dat, STD_SEC) != ados_hdr.dat_checksum) ||
            (ados_hdr.sector != sec) || (ados_hdr.format != 0xffu) ||
            (ados_hdr.track != tracknr) ||
            (ados_hdr.sectors_to_gap != (11-sec))) {
            INFO("Fail %u header\n", sec);
            goto fail;
        }

        /* AmigaDOS inter-sector gaps must all be zero. */
        if ((sec != 10) && ((gap[0]|gap[1]) != 0)) {
            INFO("Fail %u gap\n", sec);
            goto fail;
        }

        /* Sanity-check the header label area. */
        switch (sec) {
        case 0: case 1:
            /* Normal empty label area in bootblock sectors. */
            if (mem_check_pattern(ados_hdr.lbl, 0x00, 16)) {
                INFO("Fail %u label\n", sec);
                goto fail;
            }
            break;
        default:
            /* First two label bytes are PC sector data CRC.
             * Remainder are 4E gap (part of the post-data GAP3). 
             * NB. CRC in sector #2 is bogus: there is no DAM in sector #1. */
            mfm_decode_bytes(bc_mfm, 16, &raw[2*4], ados_hdr.lbl);
            if (mem_check_pattern(&ados_hdr.lbl[2], 0x4e, 16-2)) {
                INFO("Fail %u label\n", sec);
                goto fail;
            }
            break;
        }

        /* Save the AmigaDOS data. */
        memcpy(&amiga_block[sec*STD_SEC], dat, STD_SEC);

        /* Now construct the IBM DAM (and the next IDAM enclosed within it). 
         * The DAM starts immediately in the AmigaDOS data area:
         * 4*00, 3*A1, FB, ... */
        memmove(raw, &raw[2*28], sizeof(raw)-2*28);
        if (stream_next_bytes(s, &raw[sizeof(raw)-2*28], 2*8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 4+4+512+2, raw, dat);

        /* First two AmigaDOS sectors do not contain a DAM, so skip DAM 
         * checks for those. */
        if (sec >= 2) {
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
            /* Save the PC data. */
            memcpy(pc_block + (ibm_secs[sec-2]-1)*512, dat + 4 + 4, 512);
        }

        /* First and last AmigaDOS sectors contain no IDAM. Skip IDAM checks 
         * for those. */
        if ((sec >= 1) && (sec <= 9)) {
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
                (dat[18] != ibm_secs[sec-1]) || (dat[19] != 2) ||
                crc16_ccitt(dat+12, 4+4+2, 0xffff)) {
                INFO("Fail %u idam\n", sec);
                goto fail;
            }
            INFO("IDAM CRC %04x\n", be16toh(*(uint16_t *)(dat+20)));
        }

        /* Shift next AmigaDOS sector header to start of the dat[] array. */
        s->word = be32toh(*(uint32_t *)&raw[sizeof(raw)-2*28]);
        memmove(raw, &raw[sizeof(raw)-2*26], 2*6);
    }

    /* Amiga bootblock checks: Must be valid bootable OFS volume. */
    if (strcmp("DOS", amiga_block) ||
        (be32toh(*(uint32_t *)&amiga_block[8]) != 880) ||
        amiga_bootblock_checksum(amiga_block) ||
        /* Most of bootblock is empty, except small executable and signature
         * region at start, and the first PC IDAM at the end. */
        mem_check_pattern(amiga_block+256, 0x00, 1024-256-2*(4+4+2))) {
        INFO("Fail bootblock\n");
        goto fail;
    }

    td = memalloc(sizeof(*td));
    memcpy(td->ami_bb, amiga_block+12, sizeof(td->ami_bb));
    memcpy(td->pc_sec1, pc_block, sizeof(td->pc_sec1));
    for (sec = 0; sec < 8; sec++)
        memcpy(td->pc_secN[sec], pc_block+(sec+1)*512,
               sizeof(td->pc_secN[sec]));

    memfree(amiga_block);
    memfree(pc_block);

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
    memfree(amiga_block);
    memfree(pc_block);
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

static void rnc_triformat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ados_hdr ados_hdr;
    struct tri_data *td = (struct tri_data *)ti->dat;
    uint8_t buf[544*11+10], *p, *q;
    uint8_t raw[2*(544*11+10)];
    uint8_t ados_dat[1024];
    unsigned int sec;
    uint32_t csum;

#define ados_to_ibm(_src, _dst, _nr)                        \
    mfm_encode_bytes(bc_mfm_even_odd, _nr, _src, raw, 0);   \
    mfm_decode_bytes(bc_mfm, _nr, raw, _dst);
#define ibm_to_ados(_src, _dst, _nr)                        \
    mfm_encode_bytes(bc_mfm, _nr, _src, raw, 0);            \
    mfm_decode_bytes(bc_mfm_even_odd, _nr, raw, _dst);

    p = buf;

    for (sec = 0; sec < 11; sec++) {

        memset(p, 0x00, 2); p += 2;
        memset(p, 0xa1, 2); p += 2;
        memset(&ados_hdr, 0, sizeof(ados_hdr));
        ados_hdr.format = 0xff;
        ados_hdr.track = tracknr;
        ados_hdr.sector = sec;
        ados_hdr.sectors_to_gap = 11-sec;
        ados_to_ibm(&ados_hdr, p, 4); p += 4;

        if (sec < 2) {
            memset(p, 0x00, 16); p += 16;
        } else {
            /* CRC */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-516, 516, 0xffff)); p += 2;
            /* GAP3 */
            memset(p, 0x4e, 14); p += 14;
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

        if (sec == 0) {
            /* AmigaDOS bootblock */
            memset(ados_dat, 0, 512);
            strcpy((char *)ados_dat, "DOS");
            *(uint32_t *)(ados_dat+8) = htobe32(880);
            memcpy(ados_dat+12, td->ami_bb, sizeof(td->ami_bb));
            ados_to_ibm(ados_dat, p, 512);
            p += 512;
        } else if (sec == 1) {
            memset(p, 0, 512-22);
            p += 512-22;
        } else {
            /* DAM */
            int pc_sec = ibm_secs[sec-2];
            memset(p, 0x00, 4); p += 4;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfb;
            if (pc_sec == 1) {
                memcpy(p, td->pc_sec1, 512); p += 512;
            } else {
                memcpy(p, td->pc_secN[pc_sec-2], 512-8-22); p += 512-8-22;
            }
        }

        /* IDAM */
        if ((sec >= 1) && (sec <= 9)) {
            memset(p, 0x00, 12); p += 12;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfe;
            *p++ = tracknr >> 1;    /* C */
            *p++ = tracknr & 1;     /* H */
            *p++ = ibm_secs[sec-1]; /* R */
            *p++ = 2;               /* N */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-8, 8, 0xffff)); /* CRC */
            p += 2;
        }

    }

    /* Final DAM CRC. */
    *(uint16_t *)p = htobe16(crc16_ccitt(p-516, 516, 0xffff)); p += 2;

    /* AmigaDOS bootblock checksum. */
    q = p;
    p = buf;
    /* Extract entire bootblock to the 1kB ADOS data buffer. */
    ibm_to_ados(p+32, ados_dat, 512);
    ibm_to_ados(p+544+32, ados_dat+512, 512);
    /* Calculate the bootblock checksum. */
    *(uint32_t *)(ados_dat+4) = htobe32(amiga_bootblock_checksum(ados_dat));
    /* Re-encode the whole first sector of the bootblock. */
    ados_to_ibm(ados_dat, p+32, 512);

    /* AmigaDOS data checksums. */
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
        if ((sec >= 1) && (sec <= 9))
            sync_fixup(p+2*(544-10), 3); /* IDAM sync */
        if (sec >= 2)
            sync_fixup(p+2*36, 3); /* DAM sync */
        p += 544*2;
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_raw, 2*(q-buf), raw);
}

static void rnc_triformat_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct tri_data *td = (struct tri_data *)ti->dat;
    unsigned int i;
    char *p;

    sectors->nr_bytes = 9*512;
    sectors->data = memalloc(sectors->nr_bytes);

    p = (char *)sectors->data;
    memcpy(p, td->pc_sec1, 512);
    p += 512;
    for (i = 0; i < 8; i++) {
        memcpy(p, td->pc_secN[i], sizeof(td->pc_secN[i]));
        p += 512;
    }
}

void *rnc_triformat_to_ados(struct disk *d, unsigned int tracknr)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct tri_data *td = (struct tri_data *)ti->dat;
    char *p = memalloc(11*512);

    /* AmigaDOS bootblock */
    strcpy(p, "DOS");
    *(uint32_t *)(p+8) = htobe32(880);
    memcpy(p+12, td->ami_bb, sizeof(td->ami_bb));
    *(uint32_t *)(p+4) = htobe32(amiga_bootblock_checksum(p));

    return p;
}

struct track_handler rnc_triformat_handler = {
    .write_raw = rnc_triformat_write_raw,
    .read_raw = rnc_triformat_read_raw,
    .read_sectors = rnc_triformat_read_sectors
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
