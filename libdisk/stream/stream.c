/******************************************************************************
 * stream/stream.c
 * 
 * Interface for stream parsers.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "private.h"

extern struct stream_type kryoflux_stream;
extern struct stream_type diskread;
extern struct stream_type disk_image;
extern struct stream_type caps;

const static struct stream_type *stream_type[] = {
    &kryoflux_stream,
    &diskread,
    &disk_image,
    &caps,
    NULL
};

struct stream *stream_open(const char *name)
{
    struct stat sbuf;
    const struct stream_type *st;
    struct stream *s;
    unsigned int i;

    /* Only Kryoflux STREAMs may be anything other than a single file. */
    if ( (stat(name, &sbuf) < 0) || S_ISDIR(sbuf.st_mode) )
    {
        st = &kryoflux_stream;
        if ( (s = st->open(name)) != NULL )
            s->type = st;
        return s;
    }

    for ( i = 0; (st = stream_type[i]) != NULL; i++ )
    {
        if ( (s = st->open(name)) != NULL )
        {
            s->type = st;
            break;
        }
    }

    return s;
}

void stream_close(struct stream *s)
{
    s->type->close(s);
}

void stream_reset(struct stream *s, unsigned int tracknr)
{
    s->nr_index = 0;
    s->latency = 0;
    s->index_offset = ~0u>>1; /* bad */
    s->type->reset(s, tracknr);
}

void stream_next_index(struct stream *s)
{
    do {
        if ( stream_next_bit(s) == -1 )
            break;
    } while ( s->index_offset != 0 );
}

int stream_next_bit(struct stream *s)
{
    int b;
    if ( s->nr_index >= 5 )
        return -1;
    s->index_offset++;
    b = s->type->next_bit(s);
    if ( b != -1 )
        s->word = (s->word << 1) | b;
    return b;
}

int stream_next_bits(struct stream *s, unsigned int bits)
{
    unsigned int i;
    for ( i = 0; i < bits; i++ )
        if ( stream_next_bit(s) == -1 )
            return -1;
    return 0;
}

int stream_next_bytes(struct stream *s, void *p, unsigned int bytes)
{
    unsigned int i;
    unsigned char *dat = p;

    for ( i = 0; i < bytes; i++ )
    {
        if ( stream_next_bits(s, 8) == -1 )
            return -1;
        dat[i] = (uint8_t)s->word;
    }

    return 0;
}

void index_reset(struct stream *s)
{
    s->track_bitlen = s->index_offset;
    s->index_offset = 0;
    s->nr_index++;
}
