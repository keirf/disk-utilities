/*
 * stream/soft.c
 * 
 * Construct a soft stream based on an in-memory image of raw track data.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/stream.h>

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

static int ss_select_track(struct stream *s, unsigned int tracknr)
{
    return 0;
}

static void ss_reset(struct stream *s)
{
    struct soft_stream *ss = container_of(s, struct soft_stream, s);
    ss->pos = 0;
}

static int ss_next_flux(struct stream *s)
{
    struct soft_stream *ss = container_of(s, struct soft_stream, s);
    uint16_t speed;
    uint8_t dat;
    int flux = 0;

    do {
        if (++ss->pos >= ss->bitlen) {
            ss_reset(s);
            s->ns_to_index = s->flux + flux;
        }
        dat = !!(ss->dat[ss->pos >> 3] & (0x80u >> (ss->pos & 7)));
        speed = ss->speed ? ss->speed[ss->pos] : 1000u;
        flux += (ss->ns_per_cell * speed) / 1000u;
    } while (!dat && (flux < 1000000 /* 1ms */));

    s->flux += flux;
    return 0;
}

static struct stream_type stream_soft = {
    .close = ss_close,
    .select_track = ss_select_track,
    .reset = ss_reset,
    .next_flux = ss_next_flux
};

struct stream *stream_soft_open(
    uint8_t *data, uint16_t *speed, uint32_t bitlen, unsigned int data_rpm)
{
    struct soft_stream *ss;

    ss = memalloc(sizeof(*ss));
    ss->dat = data;
    ss->speed = speed;
    ss->bitlen = bitlen;
    ss->ns_per_cell = track_nsecs_from_rpm(data_rpm) / ss->bitlen;

    stream_setup(&ss->s, &stream_soft, data_rpm, data_rpm);

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
