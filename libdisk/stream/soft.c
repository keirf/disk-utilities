/*
 * stream/soft.c
 * 
 * Construct a soft stream based on an in-memory image of raw track data.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

struct soft_stream {
    struct stream s;
    uint8_t *dat;
    uint16_t *speed;
    uint32_t pos, bitlen, ns_per_cell;
};

static void ss_close(struct stream *s)
{
    struct soft_stream *ss = container_of(s, struct soft_stream, s);
    memfree(ss);
}

static void ss_reset(struct stream *s, unsigned int tracknr)
{
    struct soft_stream *ss = container_of(s, struct soft_stream, s);
    index_reset(s);
    ss->pos = 0;
}

static int ss_next_bit(struct stream *s)
{
    struct soft_stream *ss = container_of(s, struct soft_stream, s);
    uint16_t speed;
    uint8_t dat;

    if (++ss->pos >= ss->bitlen) {
        ss->pos = 0;
        index_reset(s);
    }

    dat = !!(ss->dat[ss->pos >> 3] & (0x80u >> (ss->pos & 7)));
    speed = ss->speed ? ss->speed[ss->pos >> 3] : 1000u;
    s->latency += (ss->ns_per_cell * speed) / 1000u;

    return dat;
}

static struct stream_type stream_soft = {
    .close = ss_close,
    .reset = ss_reset,
    .next_bit = ss_next_bit
};

struct stream *stream_soft_open(
    uint8_t *data, uint16_t *speed, uint32_t bitlen)
{
    struct soft_stream *ss;

    ss = memalloc(sizeof(*ss));
    ss->dat = data;
    ss->speed = speed;
    ss->bitlen = bitlen;
    ss->ns_per_cell = 200000000u / ss->bitlen;

    ss->s.type = &stream_soft;

    return &ss->s;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
