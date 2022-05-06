/*
 * disk/wjs_design.c
 *
 * Custom format as used for WJS Design games from Psyclapse/Psygnosis.
 *
 * Supported Games
 * Baal
 * Anarchy
 * Beastlord
 * Creatures
 * Ork
 * Spell Bound
 * 
 * Written in 2014/2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489
 * Anarchy:
 *  u32 0x534f5442 ('ANAM')
 *  u32 dat[6200/4]
 *  u32 checksum
 * Baal:
 *  u32 0x42535432 ('BAAL')
 *  u32 dat[6200/4]
 * Ork Disk 1:
 *  u32 0x4f524b31 ('ORK1')
 *  u32 dat[6232/4] 
 *  u32 checksum
 * Ork Disk 2:
 *  u32 0x4f524b32 ('ORK2')
 *  u32 dat[6232/4] 
 *  u32 checksum
 * Beastlord Disk 1
 *  u32 0x424d2d31 ('BM-1')
 *  u32 dat[6232/4] 
 *  u32 checksum
 * Beastlord Disk 2:
 *  u32 0x424d2d32 ('BM-2')
 *  u32 dat[6232/4]
 *  u32 checksum
 * Creatures Disk 1
 *  u32 0x43525431 ('CRT1')
 *  u32 dat[6232/4] 
 *  u32 checksum
 * Creatures Disk 2:
 *  u32 0x43525432 ('CRT2')
 *  u32 dat[6232/4]
 *  u32 checksum
 * Spell Bound:
 *  u32 0x46495245 ('FIRE')
 *  u32 dat[6232/4]
 *  u32 checksum
 * 
 * TRKTYP_anarchy data layout:
 *  u8 sector_data[6200]
 * TRKTYP_baal data layout:
 *  u8 sector_data[6200]
 * TRKTYP_ork_a data layout:
 *  u8 sector_data[6232]
 * TRKTYP_ork_b data layout:
 *  u8 sector_data[6232]
 * TRKTYP_beastlord_a data layout:
 *  u8 sector_data[6232]
 * TRKTYP_beastlord_b data layout:
 *  u8 sector_data[6232]
 * TRKTYP_creatures_a data layout:
 *  u8 sector_data[6232]
 * TRKTYP_creatures_b data layout:
 *  u8 sector_data[6232]
 * TRKTYP_spell_bound data layout:
 *  u8 sector_data[6232]
 */

#include <libdisk/util.h>
#include <private/disk.h>


struct wjs_info {
    uint16_t type;
    uint32_t sig;
    unsigned int bitlen;
};

const static struct wjs_info wjs_infos[] = {
    { TRKTYP_anarchy, 0x414e414d, 100500 },
    { TRKTYP_baal, 0x4241414c, 100500 },
    { TRKTYP_ork_a, 0x4f524b31, 105800 },
    { TRKTYP_ork_b, 0x4f524b32, 105800 },
    { TRKTYP_beastlord_a, 0x424d2d31, 103000 },
    { TRKTYP_beastlord_b, 0x424d2d32, 103000 },
    { TRKTYP_creatures_a, 0x43525431, 105800 },
    { TRKTYP_creatures_b, 0x43525432, 105800 },
    { TRKTYP_spell_bound, 0x46495245, 105800 }
};

static const struct wjs_info *find_wjs_info(uint16_t type)
{
    const struct wjs_info *wjs_info;
    for (wjs_info = wjs_infos; wjs_info->type != type; wjs_info++)
        continue;
    return wjs_info;
}

static void *wjs_design_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct wjs_info *wjs_info = find_wjs_info(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);
        if (be32toh(dat[0]) != wjs_info->sig)
            continue;

        csum = 0;
        if (ti->type != TRKTYP_baal){
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
            csum = be32toh(csum);
        }

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(dat[i]);
        }

        if (ti->type != TRKTYP_baal)
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

static void wjs_design_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct wjs_info *wjs_info = find_wjs_info(ti->type);
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, wjs_info->sig);

    if (ti->type != TRKTYP_baal) {
        for (i = csum = 0; i < ti->len/4; i++)
            csum ^= be32toh(dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
    }

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler anarchy_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler baal_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler ork_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler ork_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler beastlord_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler beastlord_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler creatures_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler creatures_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
};

struct track_handler spell_bound_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_write_raw,
    .read_raw = wjs_design_read_raw
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
