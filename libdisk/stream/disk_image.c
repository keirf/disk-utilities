/*
 * stream/disk_image.c
 * 
 * Convert a disk image into stream format.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <libdisk/disk.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct di_stream {
    struct stream s;
    struct disk *d;

    /* Current track info */
    unsigned int track;
    struct track_mfm *track_mfm;
    uint32_t pos, ns_per_cell;
};

static struct stream *di_open(const char *name)
{
    struct di_stream *dis;
    struct disk *d;

    if ((d = disk_open(name, 1)) == NULL)
        return NULL;

    dis = memalloc(sizeof(*dis));
    dis->d = d;
    dis->track = ~0u;

    return &dis->s;
}

static void di_close(struct stream *s)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);
    track_mfm_put(dis->track_mfm);
    disk_close(dis->d);
    memfree(dis);
}

static int di_select_track(struct stream *s, unsigned int tracknr)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);

    if (dis->track == tracknr)
        return 0;

    dis->track = ~0u;
    track_mfm_put(dis->track_mfm);
    dis->track_mfm = track_mfm_get(dis->d, tracknr);
    if (dis->track_mfm == NULL)
        return -1;
    dis->track = tracknr;
    dis->ns_per_cell = 200000000u / dis->track_mfm->bitlen;

    return 0;
}

static void di_reset(struct stream *s)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);

    if (dis->track_mfm->has_weak_bits) {
        unsigned int tracknr = dis->track;
        dis->track = ~0u;
        if (di_select_track(s, tracknr))
            BUG();
    }

    index_reset(s);
    dis->pos = 0;
}

static int di_next_bit(struct stream *s)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);
    uint8_t dat;

    if (++dis->pos >= dis->track_mfm->bitlen)
        di_reset(s);

    dat = !!(dis->track_mfm->mfm[dis->pos >> 3] & (0x80u >> (dis->pos & 7)));
    s->latency += (dis->ns_per_cell *
                   dis->track_mfm->speed[dis->pos >> 3]) / 1000u;

    return dat;
}

struct stream_type disk_image = {
    .open = di_open,
    .close = di_close,
    .select_track = di_select_track,
    .reset = di_reset,
    .next_bit = di_next_bit,
    .suffix = { "adf", "eadf", "dsk", NULL }
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
