/*
 * disk/jinks.c
 * 
 * Custom protection track format as used on Jinks by
 * Diamond Software / Rainbow Arts.
 * 
 * Written in 2014 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x92429242
 *  u32 0xaa1191aa (track 158)
 * 
 * Normal length track.
 * 
 * Protection reads the longword following sync from track 158. Converts this
 * to an offset X. Then syncs to track 159, then steps immediately to track
 * 161 and does an unsynced read of 512 words. Then expects to find sync 9242
 * at around offset X in the MFM buffer.
 * 
 * This is obviously quite imprecise, so we make the check a dead certainty
 * by stamping 9242 sync throughout track 161. We adjust this track's start
 * point to provide a large landing area for the protection check.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *jinks_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x92429242)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (tracknr == 161)
            ti->data_bitoff -= 1000;

        return memalloc(0);
    }

    return NULL;
}

static void jinks_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int nr = (tracknr == 161) ? 3000 : 1;
    while (nr--)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92429242);
    if (tracknr == 158)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaa1191aa);
}

struct track_handler jinks_handler = {
    .write_raw = jinks_write_raw,
    .read_raw = jinks_read_raw
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
