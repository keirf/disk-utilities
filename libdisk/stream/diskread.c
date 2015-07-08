/*
 * stream/diskread.c
 * 
 * Parse data from the Amiga 'diskread' utility.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/stream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct dr_stream {
    struct stream s;
    int fd;

    /* Current track number. */
    unsigned int track;

    /* Raw track data. */
    unsigned char *dat;

    unsigned int dat_idx;    /* current index into dat[] */
    uint8_t b, bpos;
    uint32_t byte_latency;
};

#define BYTES_PER_TRACK (128*1024)
#define TRACKS_PER_FILE 160
#define BYTES_PER_FILE (BYTES_PER_TRACK*TRACKS_PER_FILE)

/* PAL Amiga CIA frequency 0.709379 MHz */
#define CIA_FREQ 709379u
#define CIA_NS_PER_TICK (1000000000u/CIA_FREQ)

static struct stream *dr_open(const char *name, unsigned int data_rpm)
{
    struct stat sbuf;
    struct dr_stream *drs;
    int fd;

    if ((stat(name, &sbuf) < 0) || (sbuf.st_size != BYTES_PER_FILE))
        return NULL;

    if ((fd = file_open(name, O_RDONLY)) == -1)
        err(1, "%s", name);

    drs = memalloc(sizeof(*drs));
    drs->fd = fd;
    drs->dat = memalloc(BYTES_PER_TRACK);
    drs->track = ~0u;

    return &drs->s;
}

static void dr_close(struct stream *s)
{
    struct dr_stream *drs = container_of(s, struct dr_stream, s);
    close(drs->fd);
    memfree(drs->dat);
    memfree(drs);
}

static int dr_select_track(struct stream *s, unsigned int tracknr)
{
    struct dr_stream *drs = container_of(s, struct dr_stream, s);

    if (drs->track == tracknr)
        return 0;

    if (tracknr >= TRACKS_PER_FILE)
        return -1;

    lseek(drs->fd, tracknr*BYTES_PER_TRACK, SEEK_SET);
    read_exact(drs->fd, drs->dat, BYTES_PER_TRACK);
    drs->track = tracknr;

    s->max_revolutions = ~0u;
    return 0;
}

static void dr_reset(struct stream *s)
{
    struct dr_stream *drs = container_of(s, struct dr_stream, s);
    unsigned int i;

    /* Skip garbage start-of-track data. */
    for (i = 16; (i < BYTES_PER_TRACK/2) && (drs->dat[2*i+1] == 0); i++)
        continue;
    drs->dat_idx = i;
    drs->bpos = 0;
}

static int dr_next_flux(struct stream *s)
{
    struct dr_stream *drs = container_of(s, struct dr_stream, s);
    int bit, flux = 0;

    do {
        if ((drs->bpos & 7) == 0) {
            if (drs->dat_idx >= BYTES_PER_TRACK/2)
                return -1;
            if ((drs->byte_latency = drs->dat[2*drs->dat_idx]) & 0x80)
                s->ns_to_index = s->flux + flux;
            drs->byte_latency &= 0x7f;
            drs->byte_latency *= CIA_NS_PER_TICK;
            drs->b = drs->dat[2*drs->dat_idx+1];
            drs->dat_idx++;
            drs->bpos = 0;
        }

        bit = (drs->b >> (7 - drs->bpos)) & 1;

        flux += drs->byte_latency >> 3;
        if (drs->bpos++ == 7)
            flux += drs->byte_latency & 7;
    } while (!bit && (flux < 1000000 /* 1ms */));

    s->flux += flux;
    return 0;
}

struct stream_type diskread = {
    .open = dr_open,
    .close = dr_close,
    .select_track = dr_select_track,
    .reset = dr_reset,
    .next_flux = dr_next_flux,
    .suffix = { "dat", NULL }
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
