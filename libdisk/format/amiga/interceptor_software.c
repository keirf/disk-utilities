/*
 * disk/interceptor_software.c
 * 
 * AmigaDOS-based protection used on various titles released under multiple
 * labels (Pandora, Players, Smash 16) by publisher Interceptor Software:
 *  Into The Eagles Nest, War Machine, etc.
 * 
 * Written in 2014 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *interceptor_software_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    stream_reset(s);

    while (stream_next_bit(s) != -1) {

        /* Sync word 0xa144 precedes the AmigaDOS block by ~2000 bits. */
        if ((uint16_t)s->word != 0xa144)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* Check for a decent-length zero sequence after the sync. */
        for (i = 0; i < 32; i++)
            if ((stream_next_bits(s, 32) == -1)
                || mfm_decode_word(s->word))
                break;
        if (i != 32)
            continue;

        /* Skip 104400 bits after the sync pattern. Check for 0xa145
         * fill pattern, repeating. */
        stream_next_bits(s, 104400-32*32);
        if (s->word != 0xa145a145)
            continue;

        init_track_info(ti, TRKTYP_interceptor_software);
        ti->total_bits = 105550;
        return ablk;
    }

fail:
    memfree(ablk);
    return NULL;
}

static void interceptor_software_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    for (i = 0; i < 140; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    for (i = 0; i < 460; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa145);
}

struct track_handler interceptor_software_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = interceptor_software_write_raw,
    .read_raw = interceptor_software_read_raw
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
