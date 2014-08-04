/*
 * disk/raw.c
 * 
 * Dumb container type for raw MFM data, as from an extended ADF.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#define MAX_BLOCK 100000u

static void *raw_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    char *block = memalloc(MAX_BLOCK);
    unsigned int i = 0;

    do {
        if ((stream_next_bits(s, 8) == -1) || (i == MAX_BLOCK)) {
            memfree(block);
            return NULL;
        }
        block[i++] = (uint8_t)s->word;
    } while (s->index_offset >= 8);

    ti->total_bits = i*8 - s->index_offset;
    ti->len = i;
    ti->data_bitoff = 0;

    return block;
}

static void raw_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    if (ti->total_bits/8)
        tbuf_bytes(tbuf, SPEED_AVG, bc_raw, ti->total_bits/8, ti->dat);
    if (ti->total_bits%8)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, ti->total_bits%8,
                  ti->dat[ti->total_bits/8] >> (8 - ti->total_bits%8));
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
