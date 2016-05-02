/*
 * disk/raw.c
 * 
 * Dumb container type for raw MFM data, as from an extended ADF.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define MAX_BYTES 100000u

static void *raw_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    char *dat = memalloc(MAX_BYTES);
    uint16_t *block = NULL, *block_p;
    uint32_t av_latency, *speed = memalloc(4 * MAX_BYTES);
    uint64_t tot_latency;
    unsigned int bytes, i;

    tot_latency = 0;
    bytes = 0;

    do {
        s->latency = 0;
        if ((stream_next_bits(s, 8) == -1) || (bytes == MAX_BYTES))
            goto out;
        dat[bytes] = (uint8_t)s->word;
        speed[bytes] = (uint32_t)s->latency;
        tot_latency += s->latency;
        bytes++;
    } while (s->index_offset_bc >= 8);

    av_latency = tot_latency / bytes;
    for (i = 0; i < bytes; i++)
        speed[i] = ((uint64_t)speed[i] * SPEED_AVG) / av_latency;

    ti->total_bits = bytes*8 - s->index_offset_bc;
    ti->len = bytes * 3; /* 2 bytes of speed per 1 byte of dat */
    ti->data_bitoff = 0;

    /* Marshal the descriptor block: speeds then data. */
    block = block_p = memalloc(ti->len);
    for (i = 0; i < bytes; i++)
        *block_p++ = speed[i];
    memcpy(block_p, dat, bytes);

out:
    memfree(dat);
    memfree(speed);
    return block;
}

static void raw_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *speed = (uint16_t *)ti->dat;
    uint8_t *dat = (uint8_t *)(speed + (ti->total_bits+7)/8);
    unsigned int i;

    for (i = 0; i < ti->total_bits/8; i++)
        tbuf_bits(tbuf, speed[i], bc_raw, 8, dat[i]);
    if (ti->total_bits%8)
        tbuf_bits(tbuf, speed[i], bc_raw, ti->total_bits%8,
                  dat[i] >> (8 - ti->total_bits%8));
}

struct track_handler raw_sd_handler = {
    .density = trkden_single,
    .write_raw = raw_write_raw,
    .read_raw = raw_read_raw
};

struct track_handler raw_dd_handler = {
    .density = trkden_double,
    .write_raw = raw_write_raw,
    .read_raw = raw_read_raw
};

struct track_handler raw_hd_handler = {
    .density = trkden_high,
    .write_raw = raw_write_raw,
    .read_raw = raw_read_raw
};

struct track_handler raw_ed_handler = {
    .density = trkden_extra,
    .write_raw = raw_write_raw,
    .read_raw = raw_read_raw
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
