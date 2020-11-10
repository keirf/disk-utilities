/*
 * disk/speedlock.c
 * 
 * Speedlock variable-density track, used on various titles.
 * 
 * The exact position of the long/short sectors can vary slightly. Compare for
 * example Xenon 2 (SPS #2234) versus Dragon's Breath (SPS #0072).
 * 
 * Written in 2012 by Keir Fraser
 * 
 * TRKTYP_speedlock data layout:
 *  No data
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct speedlock_info {
    uint16_t offs, len;
};

static void *speedlock_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct speedlock_info *si;
    unsigned int i, len;
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
    offs[0] = s->index_offset_bc;

    /* Scan for short bitcells (shorter than -8%). */
    do {
        s->latency = 0;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    } while (s->latency > ((latency * 92) / 100));
    offs[1] = s->index_offset_bc;

    /* Scan for normal bitcells (longer than -2%). */
    do {
        s->latency = 0;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
    } while (s->latency < ((latency * 98) / 100));
    offs[2] = s->index_offset_bc;

    /* Check that each of the above regions is in correct relative order. */
    if ((offs[1] < offs[0]) || (offs[2] < offs[1]))
        goto fail;

    /* The long-bitcell region starts around 77500 bits after the index.
     * Check for that, with plenty of slack. */
    if ((offs[0] < 75000) || (offs[0] > 80000))
        goto fail;

    /* Each sector should be around 640 bits long.
     * Check for this, with plenty of slack. */
    len = (offs[2] - offs[0]) / 2;
    if ((len < 500) || (len > 800))
        goto fail;

    /* Round the offset a bit, to counteract jitter and index misalignment. */
    offs[0] = (offs[0] + 64) & ~127;

    ti->len = sizeof(*si);
    si = memalloc(ti->len);
    si->offs = offs[0] / 16;
    si->len = 640 / 16; /* hardcoded for now */

    ti->data_bitoff = 0;
    return si;

fail:
    return NULL;
}

static void speedlock_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct speedlock_info *si = (struct speedlock_info *)ti->dat;
    unsigned int i;

    for (i = 0; i < si->offs; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    tbuf_gap(tbuf, SPEED_AVG, 0);

    for (i = 0; i < si->len; i++)
        tbuf_bits(tbuf, (SPEED_AVG*110)/100, bc_mfm, 8, 0);
    tbuf_gap(tbuf, (SPEED_AVG*110)/100, 0);

    for (i = 0; i < si->len; i++)
        tbuf_bits(tbuf, (SPEED_AVG*90)/100, bc_mfm, 8, 0);
    tbuf_gap(tbuf, (SPEED_AVG*90)/100, 0);
}

struct track_handler speedlock_handler = {
    .write_raw = speedlock_write_raw,
    .read_raw = speedlock_read_raw
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
