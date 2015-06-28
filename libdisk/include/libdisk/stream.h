/*
 * libdisk/stream.h
 * 
 * Interface for stream parsers.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __LIBDISK_STREAM_H__
#define __LIBDISK_STREAM_H__

#include <stdint.h>
#include <libdisk/util.h>

enum pll_mode {
    PLL_fixed_clock, /* Fixed clock, snap phase to flux transitions. */
    PLL_variable_clock, /* Variable clock, snap phase to flux transitions. */
    PLL_authentic /* Variable clock, do not snap phase to flux transition. */
};

/* Default mode: seems to work well for most things. */
#define PLL_default PLL_authentic

struct stream {
    const struct stream_type *type;

    /* Accumulated read latency in nanosecs. Can be reset by the caller. */
    uint64_t latency;

    /* N = last bitcell returned was Nth full bitcell after index pulse. */
    uint32_t index_offset_bc; /* offset in bitcells (=N) */
    uint32_t index_offset_ns; /* offset in nanoseconds */

    /* Distance between the most recent two index pulses. */
    uint32_t track_len_bc; /* in bitcells */
    uint32_t track_len_ns; /* in nanoseconds */

    /* Number of index pulses seen so far. */
    uint32_t nr_index;

    /* Most recent 32 bits read from the stream. */
    uint32_t word;

    /* Rolling CRC-CCITT of incoming data. */
    uint16_t crc16_ccitt;
    uint8_t  crc_bitoff;

    /* RPM of drive which created this stream. */
    unsigned int drive_rpm;

    /* Recording RPM of floppy data. */
    unsigned int data_rpm;

    /* Flux-based streams: Authentic emulation of FDC PLL behaviour? */
    enum pll_mode pll_mode;

    /* Flux-based streams. */
    int flux;                /* Nanoseconds to next flux reversal */
    int clock, clock_centre; /* Clock base value in nanoseconds */
    unsigned int clocked_zeros;
    int ns_to_index;         /* Distance to next index pulse */
};

#pragma GCC visibility push(default)
struct stream *stream_open(
    const char *name, unsigned int drive_rpm, unsigned int data_rpm);
struct stream *stream_soft_open(
    uint8_t *data, uint16_t *speed, uint32_t bitlen, unsigned int data_rpm);
void stream_close(struct stream *s);
int stream_select_track(struct stream *s, unsigned int tracknr);
void stream_reset(struct stream *s);
void stream_next_index(struct stream *s);
int stream_next_bit(struct stream *s);
int stream_next_bits(struct stream *s, unsigned int bits);
int stream_next_bytes(struct stream *s, void *p, unsigned int bytes);
void stream_start_crc(struct stream *s);
enum pll_mode stream_pll_mode(struct stream *s, enum pll_mode pll_mode);
void stream_set_density(struct stream *s, unsigned int ns_per_cell);
#pragma GCC visibility pop

#endif /* __LIBDISK_STREAM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
