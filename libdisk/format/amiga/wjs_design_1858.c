/*
 * disk/wjs_design_1858.c
 *
 * Custom format as used on Beastlord, Creatures, Ork, and Spell Bound
 * by Psyclapse/Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2924,0x9492,0x4a45,0x2511 :: Beastlord Disk 1 Sync
 *  u16 0x4489,0x2924,0x9491,0x4a45,0x2512 :: Beastlord Disk 2 Sync
 *  u16 0x4489,0x2929,0x2a92,0x4952,0x5491 :: Creatures Disk 1 Sync
 *  u16 0x4489,0x2929,0x2a91,0x4952,0x5492 :: Creatures Disk 2 Sync
 *  u16 0x4489,0x2529,0x2512,0x4552,0x4911 :: Ork Disk 1 Sync
 *  u16 0x4489,0x2529,0x2511,0x4552,0x4912 :: Ork Disk 2 Sync
 *  u16 0x4489,0x2924,0xa92a,0x4449,0x5245 :: Spell Bound Sync
 *  u32 checksum
 *  u32 dat[6232/4]
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[6232]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct wjs_info {
    uint16_t type;
    uint32_t sig[2];
    unsigned int bitlen;
};

const static struct wjs_info wjs_infos[] = {
    { TRKTYP_ork_a, { 0x25292512, 0x45524911 }, 105800 },
    { TRKTYP_ork_b, { 0x25292511, 0x45524912 }, 105800 },
    { TRKTYP_beastlord_a, { 0x29249492, 0x4a452511 }, 103000 },
    { TRKTYP_beastlord_b, { 0x29249491, 0x4a452512 }, 103000 },
    { TRKTYP_creatures_a, { 0x29292a92, 0x49525491 }, 105800 },
    { TRKTYP_creatures_b, { 0x29292a91, 0x49525492 }, 105800 },
    { TRKTYP_spell_bound, { 0x2924a92a, 0x44495245 }, 105800 }
};

static const struct wjs_info *find_wjs_info(uint16_t type)
{
    const struct wjs_info *wjs_info;
    for (wjs_info = wjs_infos; wjs_info->type != type; wjs_info++)
        continue;
    return wjs_info;
}

static void *wjs_design_1858_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct wjs_info *wjs_info = find_wjs_info(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x616], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != wjs_info->sig[0])
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != wjs_info->sig[1])
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
        sum = be32toh(sum);

        for (i = csum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum ^= be32toh(dat[i]);
        }

        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = wjs_info->bitlen;
        return block;
    }

fail:
    return NULL;
}



static void wjs_design_1858_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct wjs_info *wjs_info = find_wjs_info(ti->type);
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, wjs_info->sig[0]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, wjs_info->sig[1]);

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler ork_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler ork_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler beastlord_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler beastlord_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler creatures_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler creatures_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler spell_bound_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
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
