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

    unsigned int dat_idx;    /* current index into dat[] */
    unsigned int stream_idx; /* current index into non-OOB data in dat[] */
    unsigned int index_pos;  /* stream_idx position of next index pulse */
};

#define MCK_FREQ (((18432000 * 73) / 14) / 2)
#define SCK_FREQ (MCK_FREQ / 2)
#define ICK_FREQ (MCK_FREQ / 16)
#define SCK_PS_PER_TICK (1000000000/(SCK_FREQ/1000))

static struct stream *kfs_open(const char *name)
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

static int kfs_select_track(struct stream *s, unsigned int tracknr)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    char trackname[strlen(kfss->basename) + 9];
    off_t sz;
    int fd;

    if (kfss->dat && (kfss->track == tracknr))
        return 0;

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

    return 0;
}

static void kfs_reset(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);

    kfss->dat_idx = kfss->stream_idx = 0;
    kfss->index_pos = ~0u;
}

static int kfs_next_flux(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    unsigned int i = kfss->dat_idx;
    unsigned char *dat = kfss->dat;
    uint32_t val = 0, flux;
    bool_t done = 0;

    if (kfss->stream_idx >= kfss->index_pos) {
        kfss->index_pos = ~0u;
        index_reset(s);
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
                /* sys_time ticks at ick_freq */
                kfss->index_pos = pos;
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

    flux = (val * (uint32_t)SCK_PS_PER_TICK) / 1000u;
    return (int)flux;
}

struct stream_type kryoflux_stream = {
    .open = kfs_open,
    .close = kfs_close,
    .select_track = kfs_select_track,
    .reset = kfs_reset,
    .next_bit = flux_next_bit,
    .next_flux = kfs_next_flux
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
