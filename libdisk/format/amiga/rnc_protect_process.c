/*
 * disk/rnc_protect_process.c
 * 
 * AmigaDOS-based weak-bit protection as used on various early releases:
 *  Barbarian: The Ultimate Warrior (Palace)
 *  Buggy Boy (Tenstar Pack)
 *  Eco (Ocean, Denton Designs) (Variant A)
 *  .. and others
 * 
 * Written in 2015 by Keir Fraser
 * 
 * Weak bits a short distance before first sector.
 * 
 * Variant A also has weak bits a short distance after the last sector.
 * 
 * TRKTYP_rnc_protect_process data layout:
 *  As AmigaDOS
 */

#include <libdisk/util.h>
#include <private/disk.h>

static int find_pattern(struct stream *s, struct track_info *ti, uint32_t *p)
{
    uint32_t x[3];
    int off;

    /* Grab 3 longwords a short distance before real track start. */
    if (stream_next_bits(s, ti->data_bitoff - 13*8) == -1)
        return -1;
    x[0] = s->word;
    if (stream_next_bits(s, 32) == -1)
        return -1;
    x[1] = s->word;
    if (stream_next_bits(s, 32) == -1)
        return -1;
    x[2] = s->word;

    /* This track revolution may be slightly offset from the recorded
     * data_bitoff. Scan for the sync mark to determine how much.  */
    while (s->index_offset_bc < ti->data_bitoff + 100) {
        if (stream_next_bit(s) == -1)
            return -1;
        if (s->word == 0x44894489)
            break;
    }

    /* If this track is skewed too far then the 32 bits of interest are 
     * outside our 3-longword window. Fail in that case. */
    off = s->index_offset_bc - (ti->data_bitoff + 63);
    if ((off <= -32) || (off >= 32))
        return 0;

    /* Shift the scan window according to this revolution's skew. */
    if (off < 0) {
        off = -off;
        p[0] = (x[1] >> off) | (x[0] << (32-off));
    } else if (off > 0) {
        p[0] = (x[1] << off) | (x[2] >> (32-off));
    } else {
        p[0] = x[1];
    }

    /* Now scan for weak bits after the last sector. */
    if (stream_next_bits(s, 544*11*16 + 24) == -1)
        return -1;
    p[1] = s->word;

    /* Success. */
    return 1;
}

static void *rnc_protect_process_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t patterns[5][2];
    unsigned int i, nr, weak_mask, type = ti->type;
    char *ablk;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados)) {
        memfree(ablk);
        return NULL;
    }

    /* Scan for disk data at the magic offset before first sync mark. */
    stream_reset(s);
    for (nr = 0; nr < 5;) {
        switch (find_pattern(s, ti, patterns[nr])) {
        case -1:
            goto done;
        case 1:
            nr++;
        }
        stream_next_index(s);
    }

done:
    /* Scan for weak bits differing across disk revolutions. */
    weak_mask = 0;
    for (i = 1; i < nr; i++) {
        if (patterns[0][0] != patterns[i][0])
            weak_mask |= 1;
        if (patterns[0][1] != patterns[i][1])
            weak_mask |= 2;
    }

    if (((type == TRKTYP_rnc_protect_process) && (weak_mask == 1))
        || ((type == TRKTYP_rnc_protect_process_a) && (weak_mask == 3)))
        init_track_info(ti, type);

    return ablk;
}

static void rnc_protect_process_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    tbuf_weak(tbuf, 32);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    if (ti->type == TRKTYP_rnc_protect_process_a) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
        tbuf_weak(tbuf, 32);
    }
}

struct track_handler rnc_protect_process_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = rnc_protect_process_write_raw,
    .read_raw = rnc_protect_process_read_raw
};

struct track_handler rnc_protect_process_a_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = rnc_protect_process_write_raw,
    .read_raw = rnc_protect_process_read_raw
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
