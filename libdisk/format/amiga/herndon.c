/*
 * disk/herndon.c
 * 
 * Herndon HLS: Long track gap protection in AmigaDOS track, by Ben Herndon.
 * 
 * Note that earlier Herndon protections use a simple long-gap check on a
 * 111000-bitcell AmigaDOS track. These are dealt with by the standard
 * AmigaDOS longtrack analyser.
 * 
 * The format supported here is a 105500-bitcell AmigaDOS track with a
 * special sync header, and a fixed pattern in the track footer (pre-index
 * track gap). So far seen in TV Sports Football only (SPS IPF 0407).
 * 
 * Written in 2019 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *herndon_hls_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk;
    int count;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        return NULL;

    stream_reset(s);

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0xa144)
                continue;

        /* SPS IPF 0407 (TV Sports Football): Reads track from sync A144. 
         * Expects to see >= 16*A145 at offset +0x32fa (+104400 bitcells). */
        stream_next_bits(s, 104400);
        count = -1;
        do {
            stream_next_bits(s, 32);
            count++;
        } while (s->word == 0xa145a145);

        if (count >= 8)
            goto found;
    }

    return NULL;

found:

    /* Build the track descriptor. */
    init_track_info(ti, TRKTYP_herndon_hls);
    ti->total_bits = 105500;
    ti->data_bitoff = 160;
    return ablk;
}

static void herndon_hls_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    for (i = 0; i < 140; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    for (i = 0; i < 450; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa145);
}

struct track_handler herndon_hls_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = herndon_hls_write_raw,
    .read_raw = herndon_hls_read_raw
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
