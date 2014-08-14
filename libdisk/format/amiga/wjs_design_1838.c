/*
 * disk/wjs_design_1838.c
 *
 * Custom format as used on Baal & Anarchy by Psyclapse/Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aa5,0x2aa4,0x4944,0x4945 :: Anarchy Only Sync
 *  u16 0x4489,0x292a,0xaaa4,0x4a49,0x4944 :: Baal Only Sync
 *  u32 checksum :: Anarchy Only (Baal does not use/have a checksum)
 *  u32 dat[6200/4]
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[6200]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *wjs_design_1838_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x60e], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        switch (ti->type) {
        case TRKTYP_anarchy:
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x2aa52aa4)
                continue;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x49444945)
                continue;
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
            sum = be32toh(sum);
            break;
        case TRKTYP_baal:
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x292aaaa4)
                continue;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x4a494944)
                continue;
            sum = 0; /* unused */
            break;
        }

        for (i = csum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum ^= be32toh(dat[i]);
        }

        if ((ti->type == TRKTYP_anarchy) && (sum != csum))
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

static void wjs_design_1838_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    switch (ti->type) {
    case TRKTYP_anarchy:
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aa52aa4);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x49444945);
        for (i = csum = 0; i < ti->len/4; i++)
            csum ^= be32toh(dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
        break;
    case TRKTYP_baal:
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x292aaaa4);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4a494944);
        break;
    }

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler anarchy_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = wjs_design_1838_write_raw,
    .read_raw = wjs_design_1838_read_raw
};

struct track_handler baal_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = wjs_design_1838_write_raw,
    .read_raw = wjs_design_1838_read_raw
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
