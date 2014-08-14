/*
 * disk/night_hunter.c
 *
 * Custom format as used on Night Hunter by UBI Soft.
 *
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync for tracks 2-55, 58-85, 136-151
 *  u16 0x8944:: Sync for tracks 86-135
 *  u16 dat[0xc00][2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data
 *
 * TRKTYP_night_hunter data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *night_hunter_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[ti->len/2], sum, csum, sync;
        unsigned int i;
        char *block;

        sync = (ti->type == TRKTYP_night_hunter_a) ? 0x4489 : 0x8944;

        if ((uint16_t)s->word != sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum += be16toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);

        if (csum != be16toh(sum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void night_hunter_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat, sync;
    unsigned int i;

    sync = (ti->type == TRKTYP_night_hunter_a) ? 0x4489 : 0x8944;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);

    for (i = csum = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        csum += be16toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
}

struct track_handler night_hunter_a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = night_hunter_write_raw,
    .read_raw = night_hunter_read_raw
};

struct track_handler night_hunter_b_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = night_hunter_write_raw,
    .read_raw = night_hunter_read_raw
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
