/*
 * disk/spherical.c
 *
 * Custom format as used on Spherical & Conqueror by Rainbow Arts.
 *
 * Written in 2012 by Keir Fraser
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aaa :: Sync for Spherical, Conqueror
 *  u16 0x4445,0x2aaa :: Sync for Conqueror
 *  u32 dat[0x500][2] :: Interleaved even/odd
 *  u32 csum[2] :: Even/odd, ADD.L sum over data
 *
 * TRKTYP_rainbow_arts data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rainbow_arts_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x501], csum, sync;
        unsigned int i;
        char *block;

        sync = (ti->type == TRKTYP_spherical) ? 0x44892aaa
            : 0x44452aaa;

        if (s->word != sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        csum -= 2*be32toh(dat[i-1]);
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101200;
        return block;
    }

fail:
    return NULL;
}

static void rainbow_arts_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    uint16_t sync;
    unsigned int i;

    sync = (ti->type == TRKTYP_spherical) ? 0x4489
        : 0x4445;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler spherical_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
};

struct track_handler conqueror_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
};


/*  
 * disk/rainbow_arts.c
 *
 * This is the protection tracks for the following games:
 *  Crystal Hammer
 *  Street Cat
 *  Mission Elevator
 *
 * Written in 2022 by Keith Krellwitz
 *
 * TRKTYPE_crystal_hammer_prot_a
 *
 *  Track 79.0
 *  u32 0x44894489 :: MFM sync
 *  u16 0x554A
 *  u16 0x52AA
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Only checks the sync and the first 2 u16's
 *
 * TRKTYPE_crystal_hammer_prot_b
 *
 *  Track 79.1
 *  u32 0x44894489 :: MFM sync
 *  u16 0x2aaa
 *  u16 0xaaaa
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Does not check the data
 *
 * TRKTYPE_crystal_hammer_prot_c
 *
 *  Track 80.1
 *  u32 0x44894489 :: MFM sync
 *  u16 0x4489
 *
 * Shifts the data at a specific offset and checks for 0x4489.w
 *
 * TRKTYPE_street_cat_prot_a
 *
 *  Track 79.0
 *  u32 0x92459245 :: MFM sync
 *  u16 0xAA94
 *  u16 0x94AA
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Only checks the sync and the first 2 u16's
 *
 * TRKTYPE_street_cat_prot_b
 *
 *  Track 79.1
 *  u32 0x92459245 :: MFM sync
 *  u16 0x2aaa
 *  u16 0xaaaa
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Does not check the data
 *
 * TRKTYPE_street_cat_prot_c
 *
 *  Tracks 80.1-81.1, 83.0
 *  u16 0x9245 :: MFM sync
 *  u16 0x9245
 *
 * Shifts the data at a specific offset and checks for 0x9245.w
 *
 * TRKTYPE_mission_elevator_prot_a
 *
 *  Track 79.0
 *  u32 0x44894489 :: MFM sync
 *  u16 0x554A
 *  u16 0x52AA
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Only checks the sync and the first 2 u16's
 *
 * TRKTYPE_mission_elevator_prot_b
 *
 *  Track 79.1
 *  u32 0x44894489 :: MFM sync
 *  u16 0x2aaa
 *  u16 0xaaaa
 *  u16 0 (13 mfm encoded 0's)
 *
 *  Does not check the data
 *
 * TRKTYPE_mission_elevator_prot_c
 *
 *  Track 80.1
 *  u32 0x44894489 :: MFM sync
 *  u16 0x4489
 *
 * Shifts the data at a specific offset and checks for 0x4489.w
 */

static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

static int check_length(struct stream *s, unsigned int min_bits)
{
    stream_next_index(s);
    return (s->track_len_bc >= min_bits);
}

struct prot_info {
    uint16_t pad1;
    uint16_t pad2;
    uint32_t sync;
    uint32_t check_length;
};

static void *rainbow_arts_prot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct prot_info *info = handlers[ti->type]->extra_data;
    while (stream_next_bit(s) != -1) {

        if (s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            continue;
        if ((uint16_t)s->word != info->pad1)
           continue;
        
        if (stream_next_bits(s, 16) == -1)
            continue;
        if ((uint16_t)s->word != info->pad2)
           continue;

        if (!check_sequence(s, 13, 0))
            continue;
        if (!check_length(s, info->check_length))
            break;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return memalloc(0);
    }

    return NULL;
}

static void rainbow_arts_prot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct prot_info *info = handlers[ti->type]->extra_data;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->pad1);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->pad2);
    for (i = 0; i < 13; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    }
}

struct track_handler street_cat_prot_a_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0xAA94,
        .pad2 = 0x94AA,
        .sync = 0x92459245,
        .check_length = 100000
    }
};

struct track_handler street_cat_prot_b_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0x2aaa,
        .pad2 = 0xaaaa,
        .sync = 0x92459245,
        .check_length = 100000
    }
};

struct track_handler crystal_hammer_prot_a_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0x554A,
        .pad2 = 0x52AA,
        .sync = 0x44894489,
        .check_length = 100000
    }
};

struct track_handler crystal_hammer_prot_b_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0x2aaa,
        .pad2 = 0xaaaa,
        .sync = 0x44894489,
        .check_length = 100000
    }
};


struct track_handler mission_elevator_prot_a_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0x554A,
        .pad2 = 0x52AA,
        .sync = 0x44894489,
        .check_length = 99900
    }
};

struct track_handler mission_elevator_prot_b_handler = {
    .write_raw = rainbow_arts_prot_write_raw,
    .read_raw = rainbow_arts_prot_read_raw,
    .extra_data = & (struct prot_info) {
        .pad1 = 0x2aaa,
        .pad2 = 0xaaaa,
        .sync = 0x44894489,
        .check_length = 99900
    }
};

struct prot_c_info {
    uint32_t sync;
    uint32_t check_length;
    unsigned int sync_count;
};

static void *rainbow_arts_prot_c_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct prot_c_info *info = handlers[ti->type]->extra_data;
    while (stream_next_bit(s) != -1) {

        if (info->sync_count == 1) {
            if ((uint16_t)s->word != (uint16_t)info->sync)
                continue;
            ti->data_bitoff = s->index_offset_bc - 15;
        }
        else {
            if (s->word != info->sync)
                continue;
            ti->data_bitoff = s->index_offset_bc - 31;
        }

        if (!check_length(s, info->check_length))
            break;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return memalloc(0);
    }

    return NULL;
}

static void rainbow_arts_prot_c_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct prot_c_info *info = handlers[ti->type]->extra_data;

    if (info->sync_count == 1)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, (u_int16_t)info->sync);
    else
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sync);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, (uint16_t)info->sync);
}

struct track_handler street_cat_prot_c_handler = {
    .write_raw = rainbow_arts_prot_c_write_raw,
    .read_raw = rainbow_arts_prot_c_read_raw,
    .extra_data = & (struct prot_c_info) {
        .sync = 0x92459245,
        .check_length = 94000,
        .sync_count = 1
    }
};

struct track_handler crystal_hammer_prot_c_handler = {
    .write_raw = rainbow_arts_prot_c_write_raw,
    .read_raw = rainbow_arts_prot_c_read_raw,
    .extra_data = & (struct prot_c_info) {
        .sync = 0x44894489,
        .check_length = 100000,
        .sync_count = 2
    }
};

struct track_handler mission_elevator_prot_c_handler = {
    .write_raw = rainbow_arts_prot_c_write_raw,
    .read_raw = rainbow_arts_prot_c_read_raw,
    .extra_data = & (struct prot_c_info) {
        .sync = 0x44894489,
        .check_length = 99900,
        .sync_count = 2
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
