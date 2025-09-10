/*
 * xtroll_dualformat.c
 *
 * Amiga/ST dual-Format boot track as used on Lethal Xcess by the ST demo
 * crew X-Troll.
 *
 * Written in 2025 by Keir Fraser
 *
 * Raw track layout is 11 AmigaDOS sectors, with the usual 00-bytes track gap.
 *
 * However, last sector has an IBM 512-byte sector embedded within it. The IDAM
 * is embedded within the AmigaDOS header label area. The DAM starts in the
 * AmigaDOS data area.
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
    /* Amiga bootblock is 2 sectors. */
    uint8_t ami_bb[2*512];
    /* Only one ST sector. */
    uint8_t st_sec1[512];
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

static void *xtroll_dualformat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *amiga_block;
    struct dual_data *dd;
    unsigned int sec;
    uint32_t idx_off = 0;
    struct ados_hdr ados_hdr;
    uint8_t dat[16+4+512+2], raw[2*(sizeof(struct ados_hdr)+STD_SEC+2+20)];
    uint8_t gap[2];
    int sizeof_raw;

    amiga_block = memalloc(512 * 11);
    dd = memalloc(sizeof(*dd));

retry:

    while ((stream_next_bit(s) != -1) && (s->word != 0x44894489))
        continue;

    idx_off = s->index_offset_bc - 31;

    for (sec = 0; sec < 11; sec++) {

        if (s->word != 0x44894489) {
            INFO("Fail %u sync %x\n", sec, s->word);
            goto fail;
        }

        sizeof_raw = sizeof(struct ados_hdr) + STD_SEC + 2;
        if (sec == 10)
            sizeof_raw += 20;
        if (stream_next_bytes(s, raw, sizeof_raw*2) == -1)
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
        if ((gap[0]|gap[1]) != 0) {
            INFO("Fail %u gap\n", sec);
            goto fail;
        }

        /* Sanity-check the header label area. */
        if ((sec != 10) && mem_check_pattern(ados_hdr.lbl, 0x00, 16)) {
            INFO("Fail %u label\n", sec);
            goto fail;
        }

        /* Save the AmigaDOS data. */
        memcpy(&amiga_block[sec*STD_SEC], dat, STD_SEC);

        /* Last AmigaDOS sector contains an ST sector. */
        if (sec == 10) {

            /* IDAM is in AmigaDOS label area. */
            mfm_decode_bytes(bc_mfm, 16, raw+2*4, dat);

            /* MFM IDAM: 12*00, 3*A1, FE, C, H, R, N, CRC, 5*00 */
            if (mem_check_pattern(dat, 0x00, 1) ||
                mem_check_pattern(dat+1, 0xa1, 3) ||
                /* ID = IDAM (FE) */
                (dat[4] != 0xfe) ||
                /* C, H = track */
                (dat[5] != (tracknr>>1)) || (dat[6] != (tracknr&1)) ||
                /* R = 1, N = 2 (512 bytes) */
                (dat[7] != 1) || (dat[8] != 2) ||
                crc16_ccitt(dat+1, 4+4+2, 0xffff) ||
                mem_check_pattern(dat+11, 0x00, 5)) {
                INFO("Fail %u idam\n", sec);
                goto fail;
            }
            INFO("IDAM CRC %04x\n", be16toh(*(uint16_t *)(dat+9)));

            /* DAM starts in AmigaDOS data area. */
            mfm_decode_bytes(bc_mfm, 16+4+512+2, raw+2*28, dat);

            /* MFM DAM: 16*00, 3*A1, FB, <data>, CRC */
            if (mem_check_pattern(dat, 0x00, 16) ||
                mem_check_pattern(dat+16, 0xa1, 3) ||
                /* ID = DAM (FB) */
                (dat[19] != 0xfb) ||
                crc16_ccitt(dat+16, 4+512+2, 0xffff)) {
                INFO("Fail %u dam\n", sec);
                goto fail;
            }
            INFO(" DAM CRC %04x\n", be16toh(*(uint16_t *)(dat+16+4+512)));

            /* Save the ST data. */
            memcpy(dd->st_sec1, dat+16+4, 512);

        }

        /* Stream in next AmigaDOS sync word. */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    }

    /* Amiga bootblock checks: Must be valid bootable OFS volume. */
    if (strcmp("DOS", amiga_block) ||
        (be32toh(*(uint32_t *)&amiga_block[8]) != 880) ||
        amiga_bootblock_checksum(amiga_block)) {
        INFO("Fail bootblock\n");
        goto fail;
    }


    /* Sectors 2-9 must be empty (all zeroes). */
    if (mem_check_pattern(amiga_block+2*512, 0x00, 8*512)) {
        INFO("Fail ados sectors 2-9\n");
        goto fail;
    }

    memcpy(dd->ami_bb, amiga_block, sizeof(dd->ami_bb));
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

static void xtroll_dualformat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct ados_hdr ados_hdr;
    struct dual_data *dd = (struct dual_data *)ti->dat;
    uint8_t buf[544*11+22], *p, *q;
    uint8_t raw[2*(544*11+22)];
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

        /* Skip AmigaDOS checksums for now. */
        p += 8;

        if ((sec == 0) || (sec == 1)) {

            /* AmigaDOS bootblock */
            memcpy(ados_dat, &dd->ami_bb[512*sec], 512);
            ados_to_ibm(ados_dat, p, 512);
            p += 512;

        } else if (sec == 10) {

            /* IDAM in ADOS label area */
            p -= 4+4+16;
            p += 1;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfe;
            *p++ = tracknr >> 1;    /* C */
            *p++ = tracknr & 1;     /* H */
            *p++ = 1;               /* R */
            *p++ = 2;               /* N */
            *(uint16_t *)p = htobe16(crc16_ccitt(p-8, 8, 0xffff)); /* CRC */
            p += 2 + 5;

            /* DAM in ADOS data area */
            p += 4 + 4;
            memset(p, 0x00, 16); p += 16;
            memset(p, 0xa1, 3); p += 3;
            *p++ = 0xfb;
            memcpy(p, dd->st_sec1, 512);
            p += 512;
            *(uint16_t *)p = htobe16(crc16_ccitt(p-516, 516, 0xffff));
            p += 2;

        } else {

            memset(ados_dat, 0x00, 512);
            ados_to_ibm(ados_dat, p, 512);
            p += 512;

        }

    }

    q = p;
    p = buf;

    /* AmigaDOS checksums. */
    for (sec = 0; sec < 11; sec++) {
        /* Header checksum */
        ibm_to_ados(p+4, ados_dat, 20);
        csum = htobe32(amigados_checksum(ados_dat, 20));
        ados_to_ibm(&csum, p+24, 4);
        /* Data checksum */
        ibm_to_ados(p+32, ados_dat, 512);
        csum = htobe32(amigados_checksum(ados_dat, 512));
        ados_to_ibm(&csum, p+28, 4);
        p += 544;
    }

    mfm_encode_bytes(bc_mfm, q-buf, buf, raw, 0);

    /* Fix up 4489 sync words. */
    p = raw;
    for (sec = 0; sec < 11; sec++) {
        sync_fixup(p+4, 2); /* AmigaDOS sync */
        if (sec == 10) {
            sync_fixup(p+2*(2+2+4+1), 3); /* IDAM sync */
            sync_fixup(p+2*(2+2+4+16+4+4+16), 3); /* DAM sync */
        }
        p += 544*2;
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_raw, 2*(q-buf), raw);
}

static void xtroll_dualformat_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct dual_data *dd = (struct dual_data *)ti->dat;

    sectors->nr_bytes = 10*512;
    sectors->data = memalloc(sectors->nr_bytes);

    memcpy(sectors->data, dd->st_sec1, sizeof(dd->st_sec1));
}

void *xtroll_dualformat_to_ados(struct disk *d, unsigned int tracknr)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct dual_data *dd = (struct dual_data *)ti->dat;
    char *p = memalloc(11*512);

    /* AmigaDOS bootblock */
    memcpy(p, dd->ami_bb, sizeof(dd->ami_bb));

    return p;
}

struct track_handler xtroll_dualformat_handler = {
    .write_raw = xtroll_dualformat_write_raw,
    .read_raw = xtroll_dualformat_read_raw,
    .read_sectors = xtroll_dualformat_read_sectors
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
