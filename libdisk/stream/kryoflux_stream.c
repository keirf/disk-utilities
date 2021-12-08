/*
 * stream/kryoflux_stream.c
 * 
 * Parse KryoFlux STREAM format, as read directly from the device.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/stream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct kfs_stream {
    struct stream s;
    char *basename;

    /* Current track number. */
    unsigned int track;

    /* Raw track data. */
    unsigned char *dat;
    unsigned int datsz;

    /* Index positions in the raw stream. */
    unsigned int *idxs;
    unsigned int idx_i;

    unsigned int dat_idx;    /* current index into dat[] */
    unsigned int stream_idx; /* current index into non-OOB data in dat[] */
};

#define MAX_INDEX 128

#define MCK_FREQ (((18432000 * 73) / 14) / 2)
#define SCK_FREQ (MCK_FREQ / 2)
#define ICK_FREQ (MCK_FREQ / 16)
#define SCK_PS_PER_TICK (1000000000/(SCK_FREQ/1000))

static struct stream *kfs_open(const char *name, unsigned int data_rpm)
{
    char track0[strlen(name) + 9];
    struct stat sbuf;
    struct kfs_stream *kfss;
    char *basename;

    basename = memalloc(strlen(name) + 2);
    strcpy(basename, name);

    sprintf(track0, "%s%02u.%u.raw", basename, 0, 0);
    if (stat(track0, &sbuf) < 0) {
        strcat(basename, "/");
        sprintf(track0, "%s%02u.%u.raw", basename, 0, 0);
        if (stat(track0, &sbuf) < 0)
            return NULL;
    }

    kfss = memalloc(sizeof(*kfss));
    kfss->basename = basename;

    return &kfss->s;
}

static void kfs_close(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    memfree(kfss->dat);
    memfree(kfss->basename);
    memfree(kfss);
}

static unsigned int *kfs_decode_index(unsigned char *dat, unsigned int datsz)
{
    unsigned int i, idx_i = 0;
    unsigned int *idxs = memalloc((MAX_INDEX+1) * sizeof(*idxs));

    for (i = 0; i < datsz; ) {
        switch (dat[i]) {
        case 0xd: /* oob */ {
            uint32_t pos;
            uint16_t sz = le16toh(*(uint16_t *)&dat[i+2]);
            i += 4;
            pos = le32toh(*(uint32_t *)&dat[i+0]);
            if (dat[i-3] == 2) { /* index */
                if (idx_i == MAX_INDEX)
                    goto fail;
                idxs[idx_i++] = pos;
            }
            i += sz;
            break;
        }
        case 0xa: /* nop3 */
        case 0xc: /* value16 */
            i++;
        case 0x00 ... 0x07:
        case 0x9: /* nop2 */
            i++;
        case 0x8: /* nop1 */
        case 0xb: /* overflow16 */
        default: /* 1-byte sample */
            i++;
            break;
        }
    }

    idxs[idx_i] = ~0u;
    return idxs;

fail:
    memfree(idxs);
    return NULL;
}

static int kfs_select_track(struct stream *s, unsigned int tracknr)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    char trackname[strlen(kfss->basename) + 9];
    off_t sz;
    int fd;

    if (kfss->dat && (kfss->track == tracknr))
        return 0;

    memfree(kfss->idxs);
    kfss->idxs = NULL;

    memfree(kfss->dat);
    kfss->dat = NULL;

    sprintf(trackname, "%s%02u.%u.raw", kfss->basename,
            cyl(tracknr), hd(tracknr));
    if ((fd = file_open(trackname, O_RDONLY)) == -1)
        return -1;
    if (((sz = lseek(fd, 0, SEEK_END)) < 0) ||
        (lseek(fd, 0, SEEK_SET) < 0))
        err(1, "%s", trackname);
    kfss->dat = memalloc(sz);
    read_exact(fd, kfss->dat, sz);
    close(fd);
    kfss->datsz = sz;
    kfss->track = tracknr;

    kfss->idxs = kfs_decode_index(kfss->dat, sz);
    if (kfss->idxs == NULL) {
        memfree(kfss->dat);
        kfss->dat = NULL;
        return -1;
    }

    s->max_revolutions = ~0u;
    return 0;
}

static void kfs_reset(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);

    kfss->dat_idx = kfss->stream_idx = 0;
    kfss->idx_i = 0;
}

static int kfs_next_flux(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    unsigned int i = kfss->dat_idx;
    unsigned char *dat = kfss->dat;
    uint32_t val = 0;
    bool_t done = 0;

    if (kfss->stream_idx >= kfss->idxs[kfss->idx_i]) {
        kfss->idx_i++;
        s->ns_to_index = s->flux;
    }

    while (!done && (i < kfss->datsz)) {
        switch (dat[i]) {
        case 0x00 ... 0x07: two_byte_sample:
            val += ((uint32_t)dat[i] << 8) + dat[i+1];
            i += 2; kfss->stream_idx += 2;
            done = 1;
            break;
        case 0x8: /* nop1 */
            i += 1; kfss->stream_idx += 1;
            break;
        case 0x9: /* nop2 */
            i += 2; kfss->stream_idx += 2;
            break;
        case 0xa: /* nop3 */
            i += 3; kfss->stream_idx += 3;
            break;
        case 0xb: /* overflow16 */
            val += 0x10000;
            i += 1; kfss->stream_idx += 1;
            break;
        case 0xc: /* value16 */
            i += 1; kfss->stream_idx += 1;
            goto two_byte_sample;
        case 0xd: /* oob */ {
            uint32_t pos;
            uint16_t sz = le16toh(*(uint16_t *)&dat[i+2]);
            i += 4;
            pos = le32toh(*(uint32_t *)&dat[i+0]);
            switch (dat[i-3]) {
            case 0x1: /* stream read */
            case 0x3: /* stream end */
                if (pos != kfss->stream_idx)
                    errx(1, "Out-of-sync during track read");
                break;
            case 0x2: /* index */
                break;
            case 0xd: /* eof */
                i = kfss->datsz;
                sz = 0;
                break;
            }
            i += sz;
            break;
        }
        default: /* 1-byte sample */
            val += dat[i];
            i += 1; kfss->stream_idx += 1;
            done = 1;
            break;
        }
    }

    kfss->dat_idx = i;

    if (!done)
        return -1;

    val = (val * (uint32_t)SCK_PS_PER_TICK) / 1000u;
    val = (val * s->drive_rpm) / s->data_rpm;
    s->flux += val;
    return 0;
}

struct stream_type kryoflux_stream = {
    .open = kfs_open,
    .close = kfs_close,
    .select_track = kfs_select_track,
    .reset = kfs_reset,
    .next_flux = kfs_next_flux,
    .suffix = { NULL }
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
