/*
 * disk/hammerfist.c
 *
 * Custom format as used on Hammerfist by Activision.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYP_hammerfist_a
 * 
 *  u8  0xA1 (4489 Sync)
 *  u32 0x54484242 ('ARB2')
 *  u32 dat[6664/4]
 * 
 * TRKTYP_hammerfist_b
 * 
 *  u8  0xA1 (4489 Sync)
 *  u32 0x424f4e44 ('BOND')
 *  u32 dat[6680/4]
 * 
 * TRKTYP_hammerfist_c
 * 
 *  u8  0xA1 (4489 Sync)
 *  u32 0x424f4e44 ('BOND')
 *  u32 dat[6700/4]
 * 
 * No checksum found
 *
 * TRKTYP_hammerfist_a data layout:
 *  u8 sector_data[6664]
 * 
 * TRKTYP_hammerfist_b data layout:
 *  u8 sector_data[6680]
 * 
 * TRKTYP_hammerfist_c data layout:
 *  u8 sector_data[6700]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct hammerfist_info {
    uint32_t sig;
};


static void *hammerfist_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hammerfist_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sig;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        /* signature */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);
        if (be32toh(sig) != info->sig)
            continue;

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        ti->total_bits = 110600;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void hammerfist_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hammerfist_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, info->sig);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler hammerfist_a_handler = {
    .bytes_per_sector = 6664,
    .nr_sectors = 1,
    .write_raw = hammerfist_write_raw,
    .read_raw = hammerfist_read_raw,
    .extra_data = & (struct hammerfist_info) {
        .sig = 0x41524232}
};

struct track_handler hammerfist_b_handler = {
    .bytes_per_sector = 6680,
    .nr_sectors = 1,
    .write_raw = hammerfist_write_raw,
    .read_raw = hammerfist_read_raw,
    .extra_data = & (struct hammerfist_info) {
        .sig = 0x424f4e44}
};

struct track_handler hammerfist_c_handler = {
    .bytes_per_sector = 6700,
    .nr_sectors = 1,
    .write_raw = hammerfist_write_raw,
    .read_raw = hammerfist_read_raw,
    .extra_data = & (struct hammerfist_info) {
        .sig = 0x424f4e44}
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