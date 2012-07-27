/*
 * stream/kryoflux_stream.c
 * 
 * Parse KryoFlux STREAM format, as read directly from the device.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

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

    int flux;                /* Nanoseconds to next flux reversal */
    int clock;               /* Clock base value in nanoseconds */
    unsigned int clocked_zeros;
};

#define MCK_FREQ (((18432000u * 73) / 14) / 2)
#define SCK_FREQ (MCK_FREQ / 2)
#define ICK_FREQ (MCK_FREQ / 16)
#define SCK_PS_PER_TICK (1000000000u/(SCK_FREQ/1000))

#define CLOCK_CENTRE  2000u  /* 2000ns = 2us */
#define CLOCK_MAX_ADJ 10u    /* +/- 10% adjustment */
#define CLOCK_MIN     (int)((CLOCK_CENTRE * (100u - CLOCK_MAX_ADJ)) / 100u)
#define CLOCK_MAX     (int)((CLOCK_CENTRE * (100u + CLOCK_MAX_ADJ)) / 100u)

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
            tracknr>>1, tracknr&1);
    if ((fd = open(trackname, O_RDONLY)) == -1)
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

    kfss->dat_idx = kfss->stream_idx = kfss->flux = kfss->clocked_zeros = 0;
    kfss->index_pos = ~0u;
    kfss->clock = CLOCK_CENTRE;
}

static uint32_t read_u16(unsigned char *dat)
{
    return ((uint32_t)dat[1] << 8) | (uint32_t)dat[0];
}

static uint32_t read_u32(unsigned char *dat)
{
    return (read_u16(&dat[2]) << 16) | read_u16(&dat[0]);
}

static bool_t kfs_next_flux(struct stream *s, uint32_t *p_flux)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    unsigned int i = kfss->dat_idx;
    unsigned char *dat = kfss->dat;
    uint32_t val = 0;
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
            uint16_t sz = read_u16(&dat[i+2]);
            i += 4;
            pos = read_u32(&dat[i+0]);
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

    *p_flux = val;
    return done;
}

static int kfs_next_bit(struct stream *s)
{
    struct kfs_stream *kfss = container_of(s, struct kfs_stream, s);
    int new_flux;

    while (kfss->flux < (kfss->clock/2)) {
        uint32_t flux;
        if (!kfs_next_flux(s, &flux))
            return -1;
        kfss->flux += (flux * SCK_PS_PER_TICK) / 1000;
        kfss->clocked_zeros = 0;
    }

    s->latency += kfss->clock;
    kfss->flux -= kfss->clock;

    if (kfss->flux >= (kfss->clock/2)) {
        kfss->clocked_zeros++;
        return 0;
    }

#if 0
    if ((kfss->clocked_zeros >= 1) && (kfss->clocked_zeros <= 3)) {
        /* In sync: adjust base clock by 10% of phase mismatch. */
        int32_t diff = kfss->flux;
        diff /= (int)(kfss->clocked_zeros + 1);
        kfss->clock += diff / 10;
    } else {
        /* Out of sync: adjust base clock towards centre. */
        kfss->clock += (CLOCK_CENTRE - kfss->clock) / 10;
    }

    /* Clamp the clock's adjustment range. */
    kfss->clock = max(CLOCK_MIN, min(CLOCK_MAX, kfss->clock));
#endif

    new_flux = s->authentic_pll ? kfss->flux/2 : 0;
    s->latency += kfss->flux - new_flux;
    kfss->flux = new_flux;

    return 1;
}

struct stream_type kryoflux_stream = {
    .open = kfs_open,
    .close = kfs_close,
    .select_track = kfs_select_track,
    .reset = kfs_reset,
    .next_bit = kfs_next_bit
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
