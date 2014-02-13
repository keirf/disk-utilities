/*
 * disk/hellwig.c
 *
 * Custom format hellwig as used by Digitek/Axxiom/Rainbow Arts.
 *
 * Powerstyx
 * Danger Freak
 * Apprentice
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489  :: Sync
 *  u16 0x4489 :: Extra sync in Format B
 *  u16 0
 *  u32 dat[5120/4]
 *  u32 dat[6200/4] :: apprentice
 *  u32 checksum
 *
 * TRKTYP_hellwig data layout:
 *  u8 sector_data[5120]
 *
 * TRKTYP_dangerfreak data layout:
 *  u8 sector_data[5120]
 *
 * TRKTYP_apprentice data layout:
 *  u8 sector_data[6200]
 */

#include <libdisk/util.h>
#include "../private.h"



static void *hellwig_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word == 0x44894489) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (s->word != 0x44892aaa)
                continue;
            ti->data_bitoff = s->index_offset - 47;
        } else if (s->word == 0x44892aaa)
            ti->data_bitoff = s->index_offset - 31;
        else
            continue;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            if(csum > 0 && csum != 0xffffffff)
                continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->total_bits = 102000;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void hellwig_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    if (ti->type == TRKTYP_hellwig_b)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44892aaa);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    if(ti->type == TRKTYP_dangerfreak)
        if (tracknr == 7 || tracknr == 9)
            csum = 0xffffffff;
    else if(ti->type == TRKTYP_hellwig_a)
        if (tracknr == 1)
            if(csum == 0x9ec821a)
                csum = 0;
            else
                csum = 0xffffffff;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler hellwig_a_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
};

struct track_handler hellwig_b_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
};

struct track_handler dangerfreak_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
};

struct track_handler apprentice_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
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
