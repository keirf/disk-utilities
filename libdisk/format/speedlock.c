/*
 * disk/speedlock.c
 * 
 * Speedlock variable-density track, used on various titles.
 * 
 * The hardcoded values for position of the long/short sectors are taken
 * from SPS IPFs, where they are used consistently to represent Speedlock
 * tracks. They obviously work. :)
 * 
 * Written in 2012 by Keir Fraser
 * 
 * TRKTYP_speedlock data layout:
 *  No data
 */

#include <libdisk/util.h>
#include "../private.h"

#include <arpa/inet.h>

static void *speedlock_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;
    uint64_t latency;
    unsigned int offs[3];

    /* Get average 32-bits latency. */
    s->latency = 0;
    for (i = 0; i < 2000; i++)
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    latency = s->latency / 2000;

    /* Scan for long bitcells (longer than +8%). */
    do {
        s->latency = 0;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    } while (s->latency < ((latency * 108) / 100));
    offs[0] = s->index_offset;

    /* Scan for short bitcells (shorter than -8%). */
    do {
        s->latency = 0;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    } while (s->latency > ((latency * 92) / 100));
    offs[1] = s->index_offset;

    /* Scan for normal bitcells (longer than -2%). */
    do {
        s->latency = 0;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    } while (s->latency < ((latency * 98) / 100));
    offs[2] = s->index_offset;

    /* Check that each of the above regions is in correct relative order. */
    if ((offs[1] < offs[0]) || (offs[2] < offs[1]))
        goto fail;

    /*
     * The long-bitcell region starts around 77500 bits after the index.
     * Check for that, with plenty of slack.
     */
    if ((offs[0] < 75000) || (offs[0] > 80000))
        goto fail;

    /*
     * Each sector should be around 640 bits long. Check for this,
     * with plenty of slack.
     */
    offs[2] = (offs[2] - offs[0]) / 2;
    if ((offs[2] < 500) || (offs[2] > 1000))
        goto fail;

    ti->data_bitoff = 0;
    return memalloc(0);

fail:
    return NULL;
}

static void speedlock_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;

    for (i = 0; i < 4864; i++) /* 77824 mfm bits */
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
    tbuf_gap(tbuf, SPEED_AVG, 0);

    for (i = 0; i < 40; i++) /* 640 mfm bits */
        tbuf_bits(tbuf, (SPEED_AVG*110)/100, MFM_all, 8, 0);
    tbuf_gap(tbuf, (SPEED_AVG*110)/100, 0);

    for (i = 0; i < 40; i++) /* 640 mfm bits */
        tbuf_bits(tbuf, (SPEED_AVG*90)/100, MFM_all, 8, 0);
    tbuf_gap(tbuf, (SPEED_AVG*90)/100, 0);
}

struct track_handler speedlock_handler = {
    .write_mfm = speedlock_write_mfm,
    .read_mfm = speedlock_read_mfm
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
