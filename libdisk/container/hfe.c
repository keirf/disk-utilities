/*
 * libdisk/container/hfe.c
 * 
 * Read/write HxC Floppy Emulator (HFE) images.
 * 
 * Written in 2015 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* HFEv3 opcodes */
enum {
    OP_nop,     /* no effect */
    OP_index,   /* index mark */
    OP_bitrate, /* +1byte: new bitrate */
    OP_skip     /* +1byte: skip 0-8 bits in next byte */
};

/* NB. Fields are little endian. */
struct disk_header {
    char sig[8];
    uint8_t formatrevision;
    uint8_t nr_tracks, nr_sides;
    uint8_t track_encoding;
    uint16_t bitrate; /* kB/s, approx */
    uint16_t rpm; /* unused, can be zero */
    uint8_t interface_mode;
    uint8_t rsvd; /* set to 1? */
    uint16_t track_list_offset;
};

/* track_encoding */
enum {
    ENC_ISOIBM_MFM,
    ENC_Amiga_MFM,
    ENC_ISOIBM_FM,
    ENC_Emu_FM,
    ENC_Unknown = 0xff
};

/* interface_mode */
enum {
    IFM_IBMPC_DD,
    IFM_IBMPC_HD,
    IFM_AtariST_DD,
    IFM_AtariST_HD,
    IFM_Amiga_DD,
    IFM_Amiga_HD,
    IFM_CPC_DD,
    IFM_GenericShugart_DD,
    IFM_IBMPC_ED,
    IFM_MSX2_DD,
    IFM_C64_DD,
    IFM_EmuShugart_DD,
    IFM_S950_DD,
    IFM_S950_HD,
    IFM_Disable = 0xfe
};

struct track_header {
    uint16_t offset;
    uint16_t len;
};

static void hfe_init(struct disk *d)
{
    _dsk_init(d, 166);
}

/* HFE dat bit order is LSB first. Switch to/from MSB first.  */
static void bit_reverse(uint8_t *block, unsigned int len)
{
    while (len--) {
        uint8_t x = *block, y, k;
        for (k = y = 0; k < 8; k++) {
            y <<= 1;
            y |= x&1;
            x >>= 1;
        }
        *block++ = y;
    }
}

static void bit_copy(void *dst, unsigned int dst_off,
                     void *src, unsigned int src_off,
                     unsigned int nr)
{
    uint8_t *s = src, *d = dst;
    unsigned int i;
    for (i = 0; i < nr; i++) {
        uint8_t x = (s[src_off/8] >> (7-(src_off&7))) & 1;
        d[dst_off/8] |= x << (7-(dst_off&7));
        src_off++; dst_off++;
    }
}

static struct container *hfe_open(struct disk *d)
{
    struct disk_header dhdr;
    struct track_header thdr;
    struct disk_info *di;
    unsigned int i, j, len;
    uint8_t *tbuf, *raw_dat[2];
    bool_t v3 = 0;

    lseek(d->fd, 0, SEEK_SET);

    read_exact(d->fd, &dhdr, sizeof(dhdr));
    if (dhdr.formatrevision != 0)
        return NULL;
    if (!strncmp(dhdr.sig, "HXCHFEV3", sizeof(dhdr.sig))) {
        v3 = 1;
    } else if (strncmp(dhdr.sig, "HXCPICFE", sizeof(dhdr.sig))) {
        return NULL;
    }

    dhdr.track_list_offset = le16toh(dhdr.track_list_offset);

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = dhdr.nr_tracks * 2;
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < dhdr.nr_tracks; i++) {
        lseek(d->fd, dhdr.track_list_offset*512 + i*4, SEEK_SET);
        read_exact(d->fd, &thdr, 4);
        thdr.offset = le16toh(thdr.offset);
        thdr.len = le16toh(thdr.len);

        /* Read into track buffer, padded up to 512-byte boundary. */
        len = (thdr.len + 0x1ff) & ~0x1ff;
        tbuf = memalloc(len);
        lseek(d->fd, thdr.offset*512, SEEK_SET);
        read_exact(d->fd, tbuf, len);
        bit_reverse(tbuf, len);

        /* Allocate track buffers and demux the data. */
        raw_dat[0] = memalloc(len/2);
        raw_dat[1] = memalloc(len/2);
        for (j = 0; j < len; j += 512) {
            memcpy(&raw_dat[0][j/2], &tbuf[j+  0], 256);
            memcpy(&raw_dat[1][j/2], &tbuf[j+256], 256);
        }
        if (v3) {
            /* HFEv3: process opcodes in the input byte stream. */
            for (j = 0; j < 2; j++) {
                uint8_t *new_dat = memalloc(len/2);
                uint8_t br = 0, *brs = memalloc(len/2+1);
                unsigned int inb = 0, outb = 0, opc, index_bc = 0, len_bc;
                while (inb/8 < len/2) {
                    brs[outb/8] = br;
                    BUG_ON(inb & 7);
                    opc = raw_dat[j][inb/8]; 
                    if ((opc & 0xf0) == 0xf0) {
                        switch (opc & 0x0f) {
                        case OP_nop:
                            inb += 8;
                            break;
                        case OP_index:
                            inb += 8;
                            index_bc = outb;
                            break;
                        case OP_bitrate:
                            br = raw_dat[j][inb/8+1];
                            inb += 2*8;
                            break;
                        case OP_skip: {
                            uint8_t skip = raw_dat[j][inb/8+1];
                            inb += 2*8 + skip;
                            BUG_ON(skip > 8);
                            bit_copy(new_dat, outb, raw_dat[j], inb, 8-skip);
                            inb += 8-skip; outb += 8-skip;
                            break;
                        }
                        default:
                            fprintf(stderr,
                                    "Unknown HFEv3 opcode %02x\n", opc);
                            BUG();
                        }
                    } else {
                        bit_copy(new_dat, outb, raw_dat[j], inb, 8);
                        inb += 8; outb += 8;
                    }
                }
                /* Rotate track so index pulse is at bit 0. */
                brs[outb/8] = br;
                len_bc = outb;
                memset(raw_dat[j], 0, len/2);
                bit_copy(raw_dat[j], 0, new_dat, index_bc, len_bc-index_bc);
                bit_copy(raw_dat[j], len_bc-index_bc, new_dat, 0, index_bc); 
                memfree(new_dat);
                /* Set up the track. */
                setup_uniform_raw_track(d, i*2+j, TRKTYP_raw_dd,
                                        len_bc, raw_dat[j]);
                /* HACK: Poke the non-uniform speed values. */
                {
                    uint16_t *s = (uint16_t *)d->di->track[i*2+j].dat;
                    unsigned int k, av_br, cur_br;
                    av_br = (7200000 + len_bc/2) / len_bc;
                    for (k = 0; k < (outb+7)/8; k++) {
                        cur_br = brs[(k+index_bc/8) % ((outb+7)/8)];
                        s[k] = cur_br ? (cur_br*SPEED_AVG + av_br/2) / av_br
                            : SPEED_AVG;
                    }
                }
                memfree(raw_dat[j]);
                memfree(brs);
            }
        } else {
            /* Original HFE */
            for (j = 0; j < 2; j++) {
                setup_uniform_raw_track(d, i*2+j, TRKTYP_raw_dd,
                                        thdr.len*4, raw_dat[j]);
                memfree(raw_dat[j]);
            }
        }

        memfree(tbuf);
    }

    return &container_hfe;
}

static void write_bits(
    struct track_raw *raw,
    uint8_t *dst,
    unsigned int len)
{
    unsigned int i, bit;
    uint8_t x;

    /* Rotate the track so gap is at index. */
    bit = raw->write_splice_bc;
    if (bit > raw->data_start_bc)
        bit = 0; /* don't mess with an already-aligned track */

    i = x = 0;
    while (i < len*8) {
        /* Consume a bit. */
        x <<= 1;
        x |= !!(raw->bits[bit>>3] & (0x80 >> (bit & 7)));
        /* Deal with byte and block boundaries. */
        if (!(++i & 7)) {
            *dst++ = x;
            /* Only half of each 512-byte block belongs to this track. */
            if (!(i & (256*8-1)))
                dst += 256;
        }
        /* Deal with wrap. */
        if (++bit >= raw->bitlen)
            bit = 0;
        /* If we consumed all bits then repeat last 16 bits as extra gap. */
        if ((i >= raw->bitlen) && !((i - raw->bitlen) & 15)) {
            int new_bit = (int)bit - 16;
            bit = (new_bit < 0) ? new_bit + raw->bitlen : new_bit;
        }
    }
}

static void hfe_close(struct disk *d)
{
    union {
        uint8_t x[512];
        struct disk_header dhdr;
        struct track_header thdr[128];
    } block;
    struct disk_info *di = d->di;
    struct track_header *thdr;
    struct track_raw *_raw[di->nr_tracks], **raw = _raw;
    unsigned int i, j, off, bitlen, bytelen, len;
    bool_t is_st, is_amiga;
    uint8_t *tbuf;

    is_st = di->nr_tracks && (di->track[0].type == TRKTYP_atari_st_720kb);
    is_amiga = di->nr_tracks && (di->track[0].type == TRKTYP_amigados);

    for (i = 0; i < di->nr_tracks; i++) {
        raw[i] = track_alloc_raw_buffer(d);
        track_read_raw(raw[i], i);
        /* Unformatted tracks are random density, so skip speed check. 
         * Also they are random length so do not share the track buffer 
         * well with their neighbouring track on the same cylinder. Truncate 
         * the random data to a default length. */
        if (di->track[i].type == TRKTYP_unformatted) {
            raw[i]->bitlen = min(raw[i]->bitlen, DEFAULT_BITS_PER_TRACK(d));
            continue;
        }
        /* HFE tracks are uniform density. */
        for (j = 0; j < raw[i]->bitlen; j++) {
            if (raw[i]->speed[j] == 1000)
                continue;
            fprintf(stderr, "*** T%u.%u: Variable-density track cannot be "
                    "correctly written to an HFE file %u\n",
                    i/2, i&1, raw[i]->speed[j]);
            break;
        }
    }

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    /* Block 0: Disk info. */
    memset(block.x, 0xff, 512);
    block.dhdr = (struct disk_header) {
        .sig = "HXCPICFE",
        .formatrevision = 0,
        .nr_tracks = di->nr_tracks / 2,
        .nr_sides = 2,
        .track_encoding = is_amiga ? ENC_Amiga_MFM : ENC_ISOIBM_MFM,
        .bitrate = htole16(250),
        .rpm = htole16(0),
        .interface_mode = (is_amiga ? IFM_Amiga_DD
                           : is_st ? IFM_AtariST_DD
                           : IFM_GenericShugart_DD),
        .rsvd = 1,
        .track_list_offset = htole16(1)
    };
    write_exact(d->fd, block.x, 512);

    /* Block 1: Track LUT. */
    memset(block.x, 0xff, 512);
    thdr = block.thdr;
    off = 2;
    for (i = 0; i < di->nr_tracks/2; i++) {
        bitlen = max(raw[i*2]->bitlen, raw[i*2+1]->bitlen);
        bytelen = ((bitlen + 7) / 8) * 2;
        thdr->offset = htole16(off);
        thdr->len = htole16(bytelen);
        off += (bytelen + 0x1ff) >> 9;
        thdr++;
    }
    write_exact(d->fd, block.x, 512);

    for (i = 0; i < di->nr_tracks/2; i++) {
        bitlen = max(raw[0]->bitlen, raw[1]->bitlen);
        bytelen = ((bitlen + 7) / 8) * 2;
        len = (bytelen + 0x1ff) & ~0x1ff;
        tbuf = memalloc(len);

        write_bits(raw[0], &tbuf[0], len/2);
        write_bits(raw[1], &tbuf[256], len/2);

        bit_reverse(tbuf, len);
        write_exact(d->fd, tbuf, len);
        memfree(tbuf);

        track_free_raw_buffer(raw[0]);
        track_free_raw_buffer(raw[1]);
        raw += 2;
    }
}

struct container container_hfe = {
    .init = hfe_init,
    .open = hfe_open,
    .close = hfe_close,
    .write_raw = dsk_write_raw
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
