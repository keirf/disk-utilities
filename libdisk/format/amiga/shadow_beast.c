/*
 * disk/shadow_beast.c
 *
 * Custom format as used on Shadow of the Beast I & II by Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2925,0x2aa9,0x5145,0x544a :: Sync for Beast 1
 *  u16 0x4489,0x2929,0x2a91,0x4a51,0x5492 :: Sync for Beast 2
 *  u32 dat[6200/4] :: Beast 1
 *  u32 dat[6300/4] :: Beast 2
 *
 * TRKTYP_shadow_beast data layout:
 *  u8 sector_data[6200]
 *
 * TRKTYP_shadow_beast_2 data layout:
 *  u8 sector_data[6300]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct beast_info {
    uint16_t type;
    uint32_t sig[2];
    unsigned int bitlen;
};

const static struct beast_info beast_infos[] = {
    { TRKTYP_shadow_beast, { 0x29252aa9, 0x5145544a }, 100400 },
    { TRKTYP_shadow_beast_2, { 0x29292a91, 0x4a515492 }, 105700 }
};

static const struct beast_info *find_beast_info(uint16_t type)
{
    const struct beast_info *beast_info;
    for (beast_info = beast_infos; beast_info->type != type; beast_info++)
        continue;
    return beast_info;
}

static void *shadow_beast_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct beast_info *beast_info = find_beast_info(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != beast_info->sig[0])
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != beast_info->sig[1])
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = beast_info->bitlen;
        return block;
    }

fail:
    return NULL;
}

static void shadow_beast_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct beast_info *beast_info = find_beast_info(ti->type);
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, beast_info->sig[0]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, beast_info->sig[1]);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler shadow_beast_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = shadow_beast_write_raw,
    .read_raw = shadow_beast_read_raw
};

struct track_handler shadow_beast_2_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = shadow_beast_write_raw,
    .read_raw = shadow_beast_read_raw
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
