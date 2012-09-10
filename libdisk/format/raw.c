/*
 * disk/raw.c
 * 
 * Dumb container type for raw MFM data, as from an extended ADF.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

static void *raw_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    return NULL;
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

struct track_handler raw_handler = {
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
