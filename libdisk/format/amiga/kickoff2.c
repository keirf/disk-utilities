/*
 * disk/kickoff2.c
 * 
 * AmigaDOS-based long-track protection.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * Track is 5312 MFM bits longer than normal.
 * The track gap must have two discontinuities, including the write splice.
 * The best way to do this is to write an offset or different pattern in
 * each of these three sections. For now, we create an explicit discontinuity
 * by extending the final sector, and optimistically rely on the write splice
 * for the other!
 *  
 * To be precise, the last sector is extended with the same footer found in
 * SPS #2191. The remainder of the track is stuffed with 0x00 filler:
 *  Decoded contents: 0x00 (130 times), 0xf0
 * 
 * TRKTYP_kickoff2 data layout:
 *  As AmigaDOS
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *kickoff2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados)) {
        memfree(ablk);
        return NULL;
    }

    stream_next_index(s);
    if (s->track_len_bc >= 104500) {
        init_track_info(ti, TRKTYP_kickoff2);
        ti->total_bits += 5312;
    }

    return ablk;
}

static void kickoff2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    /* Extend the last sector. */
    for (i = 0; i < 130; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xf0);
}

struct track_handler kickoff2_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = kickoff2_write_raw,
    .read_raw = kickoff2_read_raw
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
