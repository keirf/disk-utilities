/*
 * rnc_dualformat.c
 *
 * Rob Northen Dual-Format track as used on boot track for Amiga/ST disks.
 * All other tracks are either solely AmigaDOS or ST (10 sectors).
 * The boot track must be readable by all systems: Amiga requires a valid
 * bootblock, and ST encodes the FAT root block in track 0.
 *
 * Written in 2020 by Keir Fraser
 *
 * Raw track layout is 11 AmigaDOS sectors, with the usual 00-bytes track gap.
 * 
 * However, all sectors after the bootblock (sectors 1-10) have a 256-byte
 * ST sector hidden within. Because the ST sector is short, it fits entirely
 * within the AmigaDOS sector data area.
 * 
 * Amiga data: Only a short bootblock is used in track 0. All other sectors
 * are merely to form a valid AmigaDOS track.
 * 
 * ST data: Only a FAT "BIOS Parameter Block" is defined, in sector 1. All
 * other sectors are empty. The secondary FAT is the only valid FAT, and this
 * begins in the first sector of track 1 side 0. ST disk is "single sided".
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

struct dual_data {
    /* Only first section of Amiga bootblock is non-zero. Conservatively save 
     * 256 bytes to cover that region. Exclude the first 12 bytes which we 
     * re-generate (bootblock signature, flags, root block, checksum). */
    uint8_t ami_bb[256-12];
    /* First ST sector is stored in full. All others are empty. */
    uint8_t st_sec1[256];
};

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

static void *rnc_dualformat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *amiga_block;
    struct dual_data *dd;
    unsigned int sec;
    uint32_t idx_off = 0;
    struct ados_hdr ados_hdr;
    uint8_t dat[4+4+512+2], raw[2*(sizeof(struct ados_hdr)+STD_SEC+2)];
    uint8_t gap[2];

    amiga_block = memalloc(512 * 11);
    dd = memalloc(sizeof(*dd));

retry:

    while ((stream_next_bit(s) != -1) && (s->word != 0x44894489))
        continue;

    idx_off = s->index_offset_bc - 31;

    for (sec = 0; sec < 11; sec++) {

        if (s->word != 0x44894489) {
            /* Gap before AmigaDOS sector 1 has a bogus extra 2*00 on 
             * earlier releases. */
            if ((sec == 1) && !mfm_decode_word(s->word))
                stream_next_bits(s, 32);
            if (s->word != 0x44894489) {
                INFO("Fail %u sync %x\n", sec, s->word);
                goto fail;
            }
        }

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;

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
            INFO("%x %x %x %x\n", ados_hdr.format, ados_hdr.track,
                 ados_hdr.sector, ados_hdr.sectors_to_gap);
            INFO("%x/%x %x/%x\n",
                 amigados_checksum(&ados_hdr, 20), ados_hdr.hdr_checksum,
                 amigados_checksum(dat, STD_SEC), ados_hdr.dat_checksum);
            goto fail;
        }

        /* AmigaDOS inter-sector gaps must all be zero. */
        if ((sec != 10) && ((gap[0]|gap[1]) != 0)) {
            INFO("Fail %u gap\n", sec);
            goto fail;
        }

        /* Sanity-check the header label area. */
        if (mem_check_pattern(ados_hdr.lbl, 0x00, 16)) {
            INFO("Fail %u label\n", sec);
            goto fail;
        }

        /* Save the AmigaDOS data. */
        memcpy(&amiga_block[sec*STD_SEC], dat, STD_SEC);

        /* First AmigaDOS sector contains no ST sector, so skip checks. */
        if (sec != 0) {

            /* IDAM pre-sync gap starts 60 bytes into the data area. */
            mfm_decode_bytes(bc_mfm, 44, raw+2*(28+60), dat);

            /* MFM IDAM: 12*00, 3*A1, FE, C, H, R, N, CRC, 22*4E */
            if (mem_check_pattern(dat, 0x00, 12) ||
                mem_check_pattern(dat+12, 0xa1, 3) ||
                /* ID = IDAM (FE) */
                (dat[15] != 0xfe) ||
                /* C, H = track */
                (dat[16] != (tracknr>>1)) || (dat[17] != (tracknr&1)) ||
                /* R = correct sector id, N = 1 (256 bytes) */
                (dat[18] != sec) || (dat[19] != 1) ||
                crc16_ccitt(dat+12, 4+4+2, 0xffff) ||
                mem_check_pattern(dat+22, 0x4e, 22)) {
                INFO("Fail %u idam\n", sec);
                goto fail;
            }
            INFO("IDAM CRC %04x\n", be16toh(*(uint16_t *)(dat+20)));

            /* DAM pre-sync gap starts after the IDAM GAP2 (22*4E). */
            mfm_decode_bytes(bc_mfm, 274, raw+2*(28+60+44), dat);

            /* MFM DAM: 12*00, 3*A1, FB, <data>, CRC */
            if (mem_check_pattern(dat, 0x00, 12) ||
                mem_check_pattern(dat+12, 0xa1, 3) ||
                /* ID = DAM (FB) */
                (dat[15] != 0xfb) ||
                crc16_ccitt(dat+12, 4+256+2, 0xffff)) {
                INFO("Fail %u dam\n", sec);
                goto fail;
            }
            INFO(" DAM CRC %04x\n", be16toh(*(uint16_t *)(dat+272)));

            if (sec == 1) {
                /* Save the ST data. */
                memcpy(dd->st_sec1, dat+16, 256);
            } else if (mem_check_pattern(dat+16, 0x00, 256)) {
                /* All ST sectors after the first are empty (all 00 bytes). */
                goto fail;
            }

        }

        /* Stream in next AmigaDOS sync word. */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    }

    /* Amiga bootblock checks: Must be valid bootable OFS volume. */
    if (strcmp("DOS", amiga_block) ||
        (be32toh(*(uint32_t *)&amiga_block[8]) != 880) ||
        amiga_bootblock_checksum(amiga_block) ||
        /* Most of bootblock is empty, except small executable and signature
         * region at start, and the ST sector enclosed inside sector 1. */
        mem_check_pattern(amiga_block+256, 0x00, 256)) {
        INFO("Fail bootblock\n");
        goto fail;
    }

    memcpy(dd->ami_bb, amiga_block+12, sizeof(dd->ami_bb));
    memfree(amiga_block);

    set_all_sectors_valid(ti);
    ti->data_bitoff = idx_off - 32; /* allow for pre-sync gap */
    ti->len = sizeof(*dd);
    return dd;

fail:
    stream_next_index(s);
    if (stream_next_bit(s) != -1) {
        INFO("Retry...\n");
        goto retry;
    }
    memfree(amiga_block);
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

static void rnc_dualformat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ados_hdr ados_hdr;
    struct dual_data *dd = (struct dual_data *)ti->dat;
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

        /* Pre-gap + sync */
        memset(p, 0x00, 2); p += 2;
        memset(p, 0xa1, 2); p += 2;

        /* Header + label */
        memset(&ados_hdr, 0, sizeof(ados_hdr));
        ados_hdr.format = 0xff;
        ados_hdr.track = tracknr;
        ados_hdr.sector = sec;
        ados_hdr.sectors_to_gap = 11-sec;
        ados_to_ibm(&ados_hdr, p, 4); p += 4;
        ados_to_ibm(ados_hdr.lbl, p, 16); p += 16;

        /* Header checksum */
        csum = htobe32(amigados_checksum(&ados_hdr, 20));
        ados_to_ibm(&csum, p, 4); p += 4;

        /* Skip AmigaDOS data checksum for now. */
        q = p += 4;

        if (sec == 0) {

            /* AmigaDOS bootblock */
            memset(ados_dat, 0, 512);
            strcpy((char *)ados_dat, "DOS");
            *(uint32_t *)(ados_dat+8) = htobe32(880);
            memcpy(ados_dat+12, dd->ami_bb, sizeof(dd->ami_bb));
            ados_to_ibm(ados_dat, p, 512);

        } else {

            memset(p, 0x4e, 512); p += 60;
            
            /* IDAM */
            memset(p, 0x00, 12); p += 12;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfe;
            *p++ = tracknr >> 1;    /* C */
            *p++ = tracknr & 1;     /* H */
            *p++ = sec;             /* R */
            *p++ = 1;               /* N */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-8, 8, 0xffff)); /* CRC */
            p += 2 + 22; /* skip CRC + GAP2 */

            /* DAM */
            memset(p, 0x00, 12); p += 12;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfb;
            if (sec == 1)
                memcpy(p, dd->st_sec1, 256);
            else
                memset(p, 0, 256);
            p += 256;
            *(uint16_t *)p = htobe16(crc16_ccitt(p-260, 260, 0xffff));

        }

        p = q + 512;

    }

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
        if (sec != 0) {
            sync_fixup(p+2*(32+60+12), 3); /* IDAM sync */
            sync_fixup(p+2*(32+60+44+12), 3); /* DAM sync */
        }
        p += 544*2;
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_raw, 2*(q-buf), raw);
}

static void rnc_dualformat_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct dual_data *dd = (struct dual_data *)ti->dat;

    sectors->nr_bytes = 10*512;
    sectors->data = memalloc(sectors->nr_bytes);

    memcpy(sectors->data, dd->st_sec1, sizeof(dd->st_sec1));
}

void *rnc_dualformat_to_ados(struct disk *d, unsigned int tracknr)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct dual_data *dd = (struct dual_data *)ti->dat;
    char *p = memalloc(11*512);

    /* AmigaDOS bootblock */
    strcpy(p, "DOS");
    *(uint32_t *)(p+8) = htobe32(880);
    memcpy(p+12, dd->ami_bb, sizeof(dd->ami_bb));
    *(uint32_t *)(p+4) = htobe32(amiga_bootblock_checksum(p));

    return p;
}

struct track_handler rnc_dualformat_handler = {
    .write_raw = rnc_dualformat_write_raw,
    .read_raw = rnc_dualformat_read_raw,
    .read_sectors = rnc_dualformat_read_sectors
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
