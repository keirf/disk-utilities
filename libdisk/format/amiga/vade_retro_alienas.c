/*
 * disk/vade_retro_alienas.c
 * 
 * Custom format used on unreleased game Vade Retro Alienas by
 * The Digital Rangers.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 4142
 *  u16 dat[0xc57][2] :: even/odd words
 *  u16 csum[2]       :: even/odd words
 * Checksum is ADD.W over all words in dat[].
 * 
 * TRKTYP_vade_retro_alienas data layout:
 *  u8 sector_data[6318]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *vade_retro_alienas_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[0xc58], raw[2], sum, i;
        char *block;

        if ((uint16_t)s->word != 0x4142)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < 0xc58; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum += be16toh(dat[i]);
        }
        sum -= be16toh(dat[0xc57]);
        if (sum != be16toh(dat[0xc57]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101500;
        return block;
    }

fail:
    return NULL;
}

static void vade_retro_alienas_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat, sum, i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4142);

    for (i = sum = 0; i < 0xc57; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        sum += be16toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, sum);
}

struct track_handler vade_retro_alienas_handler = {
    .bytes_per_sector = 6318,
    .nr_sectors = 1,
    .write_raw = vade_retro_alienas_write_raw,
    .read_raw = vade_retro_alienas_read_raw
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
