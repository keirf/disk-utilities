/*
 * stream/stream.c
 * 
 * Interface for stream parsers.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <libdisk/util.h>
#include "private.h"
#include "../private.h"

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
    const char *suffix, *const *suffix_list;
    struct stream *s;
    unsigned int i;

    /* Only Kryoflux STREAMs may be anything other than a single file. */
    if ((stat(name, &sbuf) < 0) || S_ISDIR(sbuf.st_mode)) {
        st = &kryoflux_stream;
        goto found;
    }

    if ((suffix = strrchr(name, '.')) == NULL)
        return NULL;
    suffix++;

    for (i = 0; (st = stream_type[i]) != NULL; i++) {
        for (suffix_list = st->suffix; *suffix_list != NULL; suffix_list++) {
            if (!strcmp(suffix, *suffix_list))
                goto found;
        }
    }

    return NULL;

found:
    if ((s = st->open(name)) != NULL)
        s->type = st;
    return s;
}

void stream_close(struct stream *s)
{
    s->type->close(s);
}

int stream_select_track(struct stream *s, unsigned int tracknr)
{
    int rc = s->type->select_track(s, tracknr);
    if (rc)
        return rc;
    stream_reset(s);
    return 0;
}

void stream_reset(struct stream *s)
{
    s->nr_index = 0;
    s->latency = 0;
    s->index_offset = ~0u>>1; /* bad */
    s->type->reset(s);
    if (s->nr_index == 0)
        stream_next_index(s);
}

void stream_next_index(struct stream *s)
{
    do {
        if (stream_next_bit(s) == -1)
            break;
    } while (s->index_offset != 0);
}

void stream_start_crc(struct stream *s)
{
    uint16_t x = htons(mfm_decode_bits(MFM_all, s->word));
    s->crc16_ccitt = crc16_ccitt(&x, 2, 0xffff);
    s->crc_bitoff = 0;
}

int stream_next_bit(struct stream *s)
{
    int b;
    if (s->nr_index >= 5)
        return -1;
    s->index_offset++;
    if ((b = s->type->next_bit(s)) == -1)
        return -1;
    s->word = (s->word << 1) | b;
    if (++s->crc_bitoff == 16) {
        uint8_t b = mfm_decode_bits(MFM_all, s->word);
        s->crc16_ccitt = crc16_ccitt(&b, 1, s->crc16_ccitt);
        s->crc_bitoff = 0;
    }
    return b;
}

int stream_next_bits(struct stream *s, unsigned int bits)
{
    unsigned int i;
    for (i = 0; i < bits; i++)
        if (stream_next_bit(s) == -1)
            return -1;
    return 0;
}

int stream_next_bytes(struct stream *s, void *p, unsigned int bytes)
{
    unsigned int i;
    unsigned char *dat = p;

    for (i = 0; i < bytes; i++) {
        if (stream_next_bits(s, 8) == -1)
            return -1;
        dat[i] = (uint8_t)s->word;
    }

    return 0;
}

void stream_authentic_pll_start(struct stream *s)
{
    s->authentic_pll++;
}

void stream_authentic_pll_end(struct stream *s)
{
    BUG_ON(s->authentic_pll == 0);
    s->authentic_pll--;
}

void stream_set_density(struct stream *s, unsigned int ns_per_cell)
{
    if (s->type->set_density)
        s->type->set_density(s, ns_per_cell);
}

void index_reset(struct stream *s)
{
    s->track_bitlen = s->index_offset;
    s->index_offset = 0;
    s->nr_index++;
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
