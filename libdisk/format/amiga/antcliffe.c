/*
 * disk/antcliffe.c
 *
 * Custom format by Gary Antcliffe as used by Hi-Tec on several games.
 *
 * Scooby-Doo and Scrappy-Doo
 * Yogi's Big Clean Up
 * Yogi's Great Escape
 * Future Bike Simulator
 * Alien World
 * Blazing Thunder
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 Sync :: Various see syncs hi_tec_infos
 *  u32 0x55555151
 *  u32 dat[6156/4] 
 *  u32 Checksum
 *
 * TRKTYP_scooby_doo data layout:
 *  u8 sector_data[6156]
 *
 * TRKTYP_yogis_big_clean_up data layout:
 *  u8 sector_data[6156]
 * 
 * TRKTYP_alien_world data layout:
 *  u8 sector_data[6156]
 * 
 * TRKTYP_blazing_thunder data layout:
 *  u8 sector_data[6156]
 * 
 * 
 * TRKTYP_antcliffe_no_checksum data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct hi_tec_info {
    uint16_t type;
    uint16_t syncs[16];
};

const static struct hi_tec_info hi_tec_infos[] = {
    { TRKTYP_scooby_doo, {
        0x5122, 0x4489, 0x8914, 0x2891, 0x2251, 0x4891, 0x2245, 0x8a44,
        0x44A2, 0x4522, 0x448A, 0x2291, 0x8912, 0xa244, 0x8944, 0x9122}
    },
    { TRKTYP_yogis_big_clean_up, {
        0x8944, 0x4489, 0x8912, 0x2251, 0x5122, 0x2891, 0x2245, 0x4522,
        0x44A2, 0xa244, 0x448A, 0x8a44, 0x8914, 0x4891, 0x2291, 0x9122 }
    },
    { TRKTYP_alien_world, {
        0x2245, 0x4489, 0x8914, 0x9122, 0x2251, 0x8a44, 0x2291, 0x4522,
        0x44A2, 0xa244, 0x448A, 0x5122, 0x2891, 0x8912, 0x8944, 0x4891 }
    },
    { TRKTYP_blazing_thunder, {
        0x8944, 0x4489, 0x8912, 0x2891, 0x2251, 0x5122, 0x2245, 0x4522,
        0x44A2, 0xa244, 0x448A, 0x8a44, 0x8914, 0x4891, 0x2291, 0x9122 }
    }
};

static const struct hi_tec_info *find_hi_tec_info(uint16_t type)
{
    const struct hi_tec_info *hi_tec_info;
    for (hi_tec_info = hi_tec_infos; hi_tec_info->type != type; hi_tec_info++)
        continue;
    return hi_tec_info;
}

static void *hi_tec_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hi_tec_info *hi_tec_info = find_hi_tec_info(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != hi_tec_info->syncs[tracknr & 0xf])
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555151)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (sum != be32toh(csum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}



static void hi_tec_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct hi_tec_info *hi_tec_info = find_hi_tec_info(ti->type);
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, hi_tec_info->syncs[tracknr & 0xf]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555151);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler scooby_doo_handler = {
    .bytes_per_sector = 6156,
    .nr_sectors = 1,
    .write_raw = hi_tec_a_write_raw,
    .read_raw = hi_tec_a_read_raw
};

struct track_handler yogis_big_clean_up_handler = {
    .bytes_per_sector = 6156,
    .nr_sectors = 1,
    .write_raw = hi_tec_a_write_raw,
    .read_raw = hi_tec_a_read_raw
};

struct track_handler alien_world_handler = {
    .bytes_per_sector = 6156,
    .nr_sectors = 1,
    .write_raw = hi_tec_a_write_raw,
    .read_raw = hi_tec_a_read_raw
};

struct track_handler blazing_thunder_handler = {
    .bytes_per_sector = 6156,
    .nr_sectors = 1,
    .write_raw = hi_tec_a_write_raw,
    .read_raw = hi_tec_a_read_raw
};

/*
 * Custom format by Gary Antcliffe as used by Hi-Tec on several games.
 *
 * Yogi's Great Escape
 * Future Bike Simulator
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: antcliffe_no_checksum Sync
 *  u32 0x55555151
 *  u16 dat[6144/2] 
 * 
 * TRKTYP_antcliffe_no_checksum data layout:
 *  u8 sector_data[6144]
 */


static void *antcliffe_no_checksum_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[ti->len/2];
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555151)
            continue;

        for (i = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void antcliffe_no_checksum_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555151);

    for (i = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
    }
}

struct track_handler antcliffe_no_checksum_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = antcliffe_no_checksum_write_raw,
    .read_raw = antcliffe_no_checksum_read_raw
};

/*
 * Custom format by Gary Antcliffe as used by Universe by Core.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 Sync :: Various see syncs array below
 *  u32 0x55555151
 *  u32 dat[6156/4] 
 *  u32 Checksum
 *
 * TRKTYP_universe data layout:
 *  u8 sector_data[6156]
 *
 */

const static uint32_t syncs[] = {
    0x89448944, 0x44894489, 0x89128912, 0x28912891, 0x22512251, 
    0x51225122, 0x22452245, 0x45224522, 0x44A244A2, 0xA244A244, 
    0x448A448A, 0x8A448A44, 0x89148914, 0x48914891, 0x22912291, 
    0x91229122
};

static void *universe_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;

        if (s->word != syncs[tracknr & 0xf])
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555151)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (sum != be32toh(csum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}


static void universe_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, syncs[tracknr & 0xf]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555151);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler universe_handler = {
    .bytes_per_sector = 6156,
    .nr_sectors = 1,
    .write_raw = universe_write_raw,
    .read_raw = universe_read_raw
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
