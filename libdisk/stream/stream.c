/*
 * stream/stream.c
 * 
 * Interface for stream parsers.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <limits.h>
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

/* Amount to adjust phase/period of our clock based on each observed flux.
 * These defaults are used until modified by stream_pll_set_parameters(). */
#define DEFAULT_PERIOD_ADJ_PCT  5
#define DEFAULT_PHASE_ADJ_PCT  60

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

static int flux_next_bit(struct stream *s);

void stream_setup(
    struct stream *s, const struct stream_type *st,
    unsigned int drive_rpm, unsigned int data_rpm)
{
    memset(s, 0, sizeof(*s));
    s->type = st;
    s->drive_rpm = drive_rpm ?: data_rpm ?: 300;
    s->data_rpm = data_rpm ?: drive_rpm ?: 300;
    s->pll_period_adj_pct = DEFAULT_PERIOD_ADJ_PCT;
    s->pll_phase_adj_pct = DEFAULT_PHASE_ADJ_PCT;
    s->clock = s->clock_centre = CLOCK_CENTRE;
    s->prng_seed = 0xae659201u;
}

struct stream *stream_open(
    const char *name, unsigned int drive_rpm, unsigned int data_rpm)
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
    if ((s = st->open(name, data_rpm)) != NULL)
        stream_setup(s, st, drive_rpm, data_rpm);

    return s;
}

void stream_close(struct stream *s)
{
    s->type->close(s);
}

int stream_select_track(struct stream *s, unsigned int tracknr)
{
    int rc;

    s->max_revolutions = 0;
    rc = s->type->select_track(s, tracknr << s->double_step);
    if (rc)
        return rc;
    s->max_revolutions = max_t(uint32_t, s->max_revolutions, 4);

    stream_reset(s);
    return 0;
}

static void _stream_reset(struct stream *s)
{
    /* Flux-based streams */
    s->flux = 0;
    s->clocked_zeros = 0;

    s->word = 0;
    s->nr_index = 0;
    s->latency = 0;
    s->index_offset_bc
        = s->index_offset_ns
        = s->track_len_bc
        = s->track_len_ns
        = (1u<<31)-1; /* bad */
    s->ns_to_index = INT_MAX;

    s->type->reset(s);
}

void stream_reset(struct stream *s)
{
    /* Reset the PLL clock, then allow 100 bit times for PLL lock. */
    s->clock = s->clock_centre;
    _stream_reset(s);
    stream_next_bits(s, 100);

    /* Now reset everything except the PLL clock. */
    _stream_reset(s);

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
    uint16_t x = htobe16(mfm_decode_word(s->word));
    s->crc16_ccitt = crc16_ccitt(&x, 2, 0xffff);
    s->crc_bitoff = 0;
}

int stream_next_bit(struct stream *s)
{
    uint64_t lat = s->latency;
    int b;
    if (s->nr_index > s->max_revolutions)
        return -1;
    s->index_offset_bc++;
    if ((b = flux_next_bit(s)) == -1)
        return -1;
    lat = s->latency - lat;
    s->index_offset_ns += lat;
    s->ns_to_index -= lat;
    if (s->ns_to_index <= 0) {
        s->track_len_bc = s->index_offset_bc;
        s->track_len_ns = s->index_offset_ns;
        s->ns_to_index = INT_MAX;
        s->index_offset_bc = s->index_offset_ns = 0;
        s->nr_index++;
    }
    s->word = (s->word << 1) | b;
    if (++s->crc_bitoff == 16) {
        uint8_t b = mfm_decode_word(s->word);
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

unsigned int stream_get_density(struct stream *s)
{
    return s->clock_centre;
}

void stream_set_density(struct stream *s, unsigned int ns_per_cell)
{
    /* Flux-based streams */
    s->clock = s->clock_centre = ns_per_cell;
}

static int flux_next_bit(struct stream *s)
{
    int new_flux;

    while (s->flux < (s->clock/2))
        if (s->type->next_flux(s) != 0)
            return -1;

    s->latency += s->clock;
    s->flux -= s->clock;

    if (s->flux >= (s->clock/2)) {
        s->clocked_zeros++;
        return 0;
    }

    /* PLL: Adjust clock frequency according to phase mismatch. 
     * eg. pll_period_adj_pct=0% -> timing-window centre freq. never changes */
    if (s->clocked_zeros <= 3) {
        /* In sync: adjust base clock by a fraction of phase mismatch. */
        s->clock += s->flux * s->pll_period_adj_pct / 100;
    } else {
        /* Out of sync: adjust base clock towards centre. */
        s->clock += (s->clock_centre - s->clock) * s->pll_period_adj_pct / 100;
    }

    /* Clamp the clock's adjustment range. */
    s->clock = max(CLOCK_MIN(s->clock_centre),
                   min(CLOCK_MAX(s->clock_centre), s->clock));

    /* PLL: Adjust clock phase according to mismatch. 
     * eg. pll_phase_adj_pct=100% -> timing window snaps to observed flux. */
    new_flux = s->flux * (100 - s->pll_phase_adj_pct) / 100;
    s->latency += s->flux - new_flux;
    s->flux = new_flux;

    s->clocked_zeros = 0;
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
