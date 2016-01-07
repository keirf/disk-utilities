/*
 * disk/starray.c
 * 
 * AmigaDOS-based weak-bit protection as used on StarRay by 
 * Hidden Treasures / Logotron.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * Extra 0xa144 sync shortly before first sector, followed by weak byte.
 * This one is tricky to reproduce as the disk controller has to lose
 * sync within 16 bitcells of the sync word in order to pass the protection.
 * It seems that the short-weak pattern I use when generating SCP images is
 * better than the original in this respect. :)
 * 
 * TRKTYP_starray data layout:
 *  As AmigaDOS
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *starray_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int distance[5];
    uint16_t pattern[5];
    unsigned int i, nr, corrupted_sync = 0;
    char *ablk;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados)) {
        memfree(ablk);
        return NULL;
    }

    /* Scan for disk data at the magic offset before first sync mark. */
    stream_reset(s);
    nr = 0;
    while ((stream_next_bit(s) != -1) && (nr < 5)) {
        /* Allow a144 and a145 but warn on latter (corrupted sync). */
        if ((s->word>>17) != (0xa144>>1))
            continue;
        corrupted_sync |= ((s->word>>16) != 0xa144);
        /* Remember the weak pattern */
        pattern[nr] = (uint16_t)s->word;
        /* Find distance to next 44894489 sync */
        distance[nr] = 0;
        while (stream_next_bit(s) != -1) {
            distance[nr]++;
            if (s->word == 0x44894489)
                break;
        }
        nr++;
    }

    if (corrupted_sync)
        trk_warn(ti, tracknr, "Sync pattern corrupted by weak byte");

    /* If any of the scanned patterns differ then we have weak bits. 
     * Mark this as a 'StarRay' special track. */
    for (i = 1; i < nr; i++) {
        if (pattern[0] != pattern[i]) {
            init_track_info(ti, TRKTYP_starray);
            break;
        }
    }

    /* If we did not find a changing pattern, allow this track anyway if 
     * the 44894489 sync word is close by. */
    if (nr && i==nr && (distance[0] < 256)) {
        trk_warn(ti, tracknr, "Weak byte not changing as expected "
                 "(%u identical occurrences)", nr);
        init_track_info(ti, TRKTYP_starray);
    }

    return ablk;
}

static void starray_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    tbuf_weak(tbuf, 32); /* overly long but it's okay */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler starray_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = starray_write_raw,
    .read_raw = starray_read_raw
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
