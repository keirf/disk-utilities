/******************************************************************************
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
    uint8_t *mfm;
    uint16_t *speed;
    uint32_t pos, bitlen, ns_per_cell;
};

static struct stream *di_open(const char *name)
{
    struct di_stream *dis;
    struct disk *d;

    if ( (d = disk_open(name, 1, 1)) == NULL )
        return NULL;

    dis = memalloc(sizeof(*dis));
    memset(dis, 0, sizeof(*dis));

    dis->d = d;
    dis->track = ~0u;

    return &dis->s;
}

static void di_close(struct stream *s)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);
    disk_close(dis->d);
    memfree(dis->mfm);
    memfree(dis->speed);
    memfree(dis);
}

static void di_reset(struct stream *s, unsigned int tracknr)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);

    if ( dis->track != tracknr )
    {
        memfree(dis->mfm);
        dis->mfm = NULL;
        memfree(dis->speed);
        dis->speed = NULL;
        track_read_mfm(dis->d, tracknr, &dis->mfm, &dis->speed, &dis->bitlen);
        dis->track = tracknr;
        dis->ns_per_cell = 200000000u / dis->bitlen;
    }

    index_reset(s);
    dis->pos = 0;
}

static int di_next_bit(struct stream *s)
{
    struct di_stream *dis = container_of(s, struct di_stream, s);
    uint8_t dat;

    if ( ++dis->pos >= dis->bitlen )
    {
        dis->pos = 0;
        index_reset(s);
        if ( s->nr_index >= 5 )
            return -1;
    }

    dat = !!(dis->mfm[dis->pos >> 3] & (0x80u >> (dis->pos & 7)));
    s->latency += (dis->ns_per_cell * dis->speed[dis->pos >> 3]) / 1000u;

    return dat;
}

struct stream_type disk_image = {
    .open = di_open,
    .close = di_close,
    .reset = di_reset,
    .next_bit = di_next_bit
};
