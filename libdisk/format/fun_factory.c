/*
 * disk/fun_factory.c
 *
 * Custom format as used by various Fun Factory releases:
 *   Rebellion
 *   Twin Turbos
 *
 * The format is same as Rainbird, but the checksum follows the data block.
 *
 * Written in 2012 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,0xff,0xff,trknr
 *  u32 data[10*512/4]
 *  u32 csum
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum.
 *
 * TRKTYP_fun_factory data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *fun_factory_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*ti->len/4], dat[ti->len/4], hdr, csum;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        if (be32toh(hdr) != (0xffffff00u | tracknr))
            continue;

        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
        if (be32toh(csum) != amigados_checksum(dat, ti->len))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void fun_factory_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, ti->len));
}

struct track_handler fun_factory_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = fun_factory_write_raw,
    .read_raw = fun_factory_read_raw
};


static void *fun_factory2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*ti->len/4], dat[ti->len/4], hdr, csum;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset - 31;

//        if (stream_next_bytes(s, raw, 8) == -1)
//            goto fail;
//        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
//        if (be32toh(hdr) != (0xffffff00u | tracknr))
//            continue;
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
        if (be32toh(csum) != amigados_checksum(dat, ti->len))
            continue;

        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);


        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void fun_factory2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, ti->len));
    //tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

}


struct track_handler fun_factory2_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = fun_factory2_write_raw,
    .read_raw = fun_factory2_read_raw
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
