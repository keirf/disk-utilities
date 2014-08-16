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
#include <unistd.h>
#include <libdisk/util.h>
#include <private/stream.h>
#include <private/disk.h>

/* Flux-based streams */
#define CLOCK_CENTRE  2000   /* 2000ns = 2us */
#define CLOCK_MAX_ADJ 10     /* +/- 10% adjustment */
#define CLOCK_MIN(_c) (((_c) * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX(_c) (((_c) * (100 + CLOCK_MAX_ADJ)) / 100)

extern struct stream_type kryoflux_stream;
extern struct stream_type diskread;
extern struct stream_type disk_image;
extern struct stream_type caps;
extern struct stream_type discferret_dfe2;
extern struct stream_type supercard_scp;

const static struct stream_type *stream_type[] = {
    &kryoflux_stream,
    &diskread,
    &disk_image,
    &caps,
    &discferret_dfe2,
    &supercard_scp,
    NULL
};

struct stream *stream_open(const char *name, unsigned int rpm)
{
    struct stat sbuf;
    const struct stream_type *st;
    const char *const *suffix_list;
    char suffix[8];
    struct stream *s;
    unsigned int i;

    /* Only Kryoflux STREAMs may be anything other than a single file. */
    if ((stat(name, &sbuf) < 0) || S_ISDIR(sbuf.st_mode)) {
        st = &kryoflux_stream;
        goto found;
    }

    filename_extension(name, suffix, sizeof(suffix));

    for (i = 0; (st = stream_type[i]) != NULL; i++) {
        for (suffix_list = st->suffix; *suffix_list != NULL; suffix_list++) {
            if (!strcmp(suffix, *suffix_list))
                goto found;
        }
    }

    return NULL;

found:
    if ((s = st->open(name, rpm)) == NULL)
        return NULL;

    s->type = st;
    s->rpm = rpm;

    /* Flux-based streams */
    s->pll_mode = PLL_default;
    s->clock = s->clock_centre = CLOCK_CENTRE;

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
    /* Flux-based streams */
    s->flux = 0;
    s->clocked_zeros = 0;
    s->clock = s->clock_centre;

    s->nr_index = 0;
    s->latency = 0;
    s->index_offset_bc = s->index_offset_ns = ~0u>>1; /* bad */

    s->type->reset(s);

    if (s->nr_index == 0)
        stream_next_index(s);
}

void stream_next_index(struct stream *s)
{
    do {
        if (stream_next_bit(s) == -1)
            break;
    } while (s->index_offset_bc != 0);
}

void stream_start_crc(struct stream *s)
{
    uint16_t x = htobe16(mfm_decode_bits(bc_mfm, s->word));
    s->crc16_ccitt = crc16_ccitt(&x, 2, 0xffff);
    s->crc_bitoff = 0;
}

int stream_next_bit(struct stream *s)
{
    uint64_t lat = s->latency;
    int b;
    if (s->nr_index >= 5)
        return -1;
    s->index_offset_bc++;
    if ((b = s->type->next_bit(s)) == -1)
        return -1;
    s->index_offset_ns += s->latency - lat;
    s->word = (s->word << 1) | b;
    if (++s->crc_bitoff == 16) {
        uint8_t b = mfm_decode_bits(bc_mfm, s->word);
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

enum pll_mode stream_pll_mode(struct stream *s, enum pll_mode pll_mode)
{
    enum pll_mode old_mode = s->pll_mode;
    s->pll_mode = pll_mode;
    return old_mode;
}

void stream_set_density(struct stream *s, unsigned int ns_per_cell)
{
    /* Flux-based streams */
    s->clock = s->clock_centre = ns_per_cell;
}

void index_reset(struct stream *s)
{
    s->track_len_bc = s->index_offset_bc;
    s->track_len_ns = s->index_offset_ns;
    s->index_offset_bc = s->index_offset_ns = 0;
    s->nr_index++;
}

int flux_next_bit(struct stream *s)
{
    int new_flux;

    while (s->flux < (s->clock/2)) {
        if ((new_flux = s->type->next_flux(s)) == -1)
            return -1;
        s->flux += new_flux;
        s->clocked_zeros = 0;
    }

    s->latency += s->clock;
    s->flux -= s->clock;

    if (s->flux >= (s->clock/2)) {
        s->clocked_zeros++;
        return 0;
    }

    if (s->pll_mode != PLL_fixed_clock) {
        /* PLL: Adjust clock frequency according to phase mismatch. */
        if ((s->clocked_zeros >= 1) && (s->clocked_zeros <= 3)) {
            /* In sync: adjust base clock by 10% of phase mismatch. */
            int diff = s->flux / (int)(s->clocked_zeros + 1);
            s->clock += diff / 10;
        } else {
            /* Out of sync: adjust base clock towards centre. */
            s->clock += (s->clock_centre - s->clock) / 10;
        }

        /* Clamp the clock's adjustment range. */
        s->clock = max(CLOCK_MIN(s->clock_centre),
                          min(CLOCK_MAX(s->clock_centre), s->clock));
    } else {
        s->clock = s->clock_centre;
    }

    /* Authentic PLL: Do not snap the timing window to each flux transition. */
    new_flux = (s->pll_mode == PLL_authentic) ? s->flux / 2 : 0;
    s->latency += s->flux - new_flux;
    s->flux = new_flux;

    return 1;
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
