/*
 * disk/aladdins_magic_lamp.c
 *
 * Custom format as used on Aladdin's Magic Lamp by New Line.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x0800 Sync
 *  u32 dat[ti->len/4]
 *
 * Looks like the protection only works because of the way 
 * the amiga memory works. The protection will search through
 * memory looking for a sync of 4489. If your amiga only has 512k,
 * It will find the sync at $86B16 actually reads from $6B16 and 
 * decodes the data.  My assumption is that track 79.1 was suppose to
 * be a standard amigados track, but with the a sync of $0800 prior
 * to the standard syncs. Or, maybe the developers used the amiga memory
 * issue to decode the correct track. Regardless, track 79.1 just
 * need to have a sync of $0800 and the rest of the data can be 
 * anything.
 * 
 * TRKTYP_aladdins_magic_lamp data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *aladdins_magic_lamp_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x0800)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void aladdins_magic_lamp_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x0800);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler aladdins_magic_lamp_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = aladdins_magic_lamp_write_raw,
    .read_raw = aladdins_magic_lamp_read_raw
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
