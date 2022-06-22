/*
 * disk/shadow_beast.c
 *
 * Custom format as used on Shadow of the Beast I & II by Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 * Updated 2022 to suport Shadow of the Beast with 0x190c track length
 *
 * RAW TRACK LAYOUT:
 *  u16 0xA1 (4489 Sync)
 * 
 * Beast 1 (0x1838):
 *  u32 0x534f5442 ('SOTB')
 *  u32 dat[6200/4]
 * 
 * Beast 1 (0x190c):
 *  u32 0x534f5442 ('SOTB')
 *  u32 dat[6412/4]
 * 
 * Beast 2:
 *  u32 0x42535432 ('BST2')
 *  u32 dat[6300/4]
 *
 * No checksum of any kind.
 * 
 * TRKTYP_shadow_beast data layout:
 *  u8 sector_data[6200]
 *
 * TRKTYP_shadow_beast_190c data layout:
 *  u8 sector_data[6412]
 *
 * TRKTYP_shadow_beast_2 data layout:
 *  u8 sector_data[6300]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct beast_info {
    uint32_t sig;
    unsigned int bitlen;
};

static void *shadow_beast_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct beast_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4];
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        /* signature */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);
        if (be32toh(dat[0]) != info->sig)
            continue;

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = info->bitlen;
        return block;
    }

fail:
    return NULL;
}

static void shadow_beast_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct beast_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, info->sig);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler shadow_beast_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = shadow_beast_write_raw,
    .read_raw = shadow_beast_read_raw,
    .extra_data = & (struct beast_info) {
        .sig = 0x534f5442,
        .bitlen = 100400
    }
};

struct track_handler shadow_beast_190c_handler = {
    .bytes_per_sector = 6412,
    .nr_sectors = 1,
    .write_raw = shadow_beast_write_raw,
    .read_raw = shadow_beast_read_raw,
    .extra_data = & (struct beast_info) {
        .sig = 0x534f5442,
        .bitlen = 105600
    }
};

struct track_handler shadow_beast_2_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = shadow_beast_write_raw,
    .read_raw = shadow_beast_read_raw,
    .extra_data = & (struct beast_info) {
        .sig = 0x42535432,
        .bitlen = 105700
    }
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
