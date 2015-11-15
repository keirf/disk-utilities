/*
 * disk/sink_or_swim.c
 *
 * Custom format as used by Sink Or Swim
 *
 * Written in 2015 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0xaaaa8914 ::  Sync
 *  u32 dat[6148/4]
 * 
 * There is no checksum of any kind (all undecoded track content is
 * gap filler).
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sink_or_swim_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if (s->word != 0xaaaa8914)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void sink_or_swim_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa8914);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler sink_or_swim_handler = {
    .bytes_per_sector = 6148,
    .nr_sectors = 1,
    .write_raw = sink_or_swim_write_raw,
    .read_raw = sink_or_swim_read_raw
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
