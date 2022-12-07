/*
 * disk/thalion.c
 *
 * Custom format as used on several games from Thalion.
 * 
 * Chambers of Shaolin
 * Seven Gates of Jambala
 * Warp
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 0x4489 0x4489 Sync
 *  u32 dat[ti->len/4]
 *
 * There is no checksum, so I calualted the checksums for the tracks
 * and have tested with multiple versions of each game
 * 
 * Chambers of Shaolin - Original Release
 * Track 1      - 0x752c1e64
 * Track 137    - 0xb8c8c9c7
 * 
 * Chambers of Shaolin - First Year Compilation
 * Track 1      - 0x1ec71617
 * Track 137    - 0xa333c487
 * 
 * Seven Gates of Jambala (Original and First Year Compilation)
 * Track 30     - 0x48f0dfdc
 * Track 158    - 0x35d08d19
 * 
 * Warp - (Original and First Year Compilation)
 * Track 158    - 0x8218f32d
 * 
 * The checksum for the second track only checks the sum of the first
 * hundred u32 as the tracks can have slight variations after that. 
 * 
 * TRKTYP_seven_gates_of_jambala data layout:
 *  u8 sector_data[6150]
 * 
 * TRKTYP_chambers_of_shaolin data layout:
 *  u8 sector_data[6150]
 * 
 * TRKTYP_warp_c data layout:
 *  u8 sector_data[6150]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint32_t warp_crcs[];

static void *thalion_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4489)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);

            if (ti->type == TRKTYP_seven_gates_of_jambala) {
                if (tracknr == 30 || (tracknr == 158 && i < 100))
                    sum += be32toh(dat[i]);
            }
            else if (ti->type == TRKTYP_chambers_of_shaolin)  {
                if (tracknr == 1 || (tracknr == 137 && i < 100))
                    sum += be32toh(dat[i]);
            }
            else if (ti->type == TRKTYP_warp_c)  {
                if (tracknr == 158)
                    sum += be32toh(dat[i]);
            }
        }

        if (tracknr != 30 && tracknr != 158 && tracknr != 1  
            && tracknr != 137)
            goto fail;

        if (ti->type == TRKTYP_seven_gates_of_jambala) {
            if (tracknr == 30 && sum != 0x48f0dfdc)
                continue;
            else if (tracknr == 158 && sum != 0x35d08d19)
                continue;
        } else if (ti->type == TRKTYP_chambers_of_shaolin) {
            if (tracknr == 1 && sum != 0x1ec71617 && sum != 0x752c1e64)
                continue;
            else if (tracknr == 137 && sum != 0xa333c487 && sum != 0xb8c8c9c7)
                continue;
        } else if (ti->type == TRKTYP_warp_c) {
            if (tracknr == 158 && sum != 0x8218f32d)
                continue;
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void thalion_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

}

struct track_handler seven_gates_of_jambala_handler = {
    .bytes_per_sector = 6150,
    .nr_sectors = 1,
    .write_raw = thalion_a_write_raw,
    .read_raw = thalion_a_read_raw
};

struct track_handler chambers_of_shaolin_handler = {
    .bytes_per_sector = 6150,
    .nr_sectors = 1,
    .write_raw = thalion_a_write_raw,
    .read_raw = thalion_a_read_raw
};

struct track_handler warp_c_handler = {
    .bytes_per_sector = 6150,
    .nr_sectors = 1,
    .write_raw = thalion_a_write_raw,
    .read_raw = thalion_a_read_raw
};

/*
 *
 * Custom format as used on Warp from Thalion.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * warp_a RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 dat[ti->len/4]
 * 
 * There is no checksum, so I calualted the checksums for the tracks
 * and have tested with multiple versions of each game
 * 
 * TRKTYP_warp_a data layout:
 *  u8 sector_data[6150]
 */

static void *thalion_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (tracknr > 136 && tracknr < 160) {
            if(sum != warp_crcs[tracknr - 137])
                continue;
        } else
            goto fail;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void thalion_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler warp_a_handler = {
    .bytes_per_sector = 6150,
    .nr_sectors = 1,
    .write_raw = thalion_b_write_raw,
    .read_raw = thalion_b_read_raw
};

/*
 *
 * Custom format as used on Warp from Thalion.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * warp_a RAW TRACK LAYOUT:
 *  u16 0x5224 0x5224 0x5224 :: Sync
 *  u32 dat[ti->len/4]
 * 
 * There is no checksum, so I calualted the checksums for the tracks
 * and have tested with multiple versions of each game
 * 
 * TRKTYP_warp_b data layout:
 *  u8 sector_data[6150]
 */

static void *thalion_c_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x52245224)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5224)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (tracknr > 136 && tracknr < 160) {
            if(sum != warp_crcs[tracknr - 137])
                continue;
        } else
            goto fail;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void thalion_c_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245224);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5224);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

}

struct track_handler warp_b_handler = {
    .bytes_per_sector = 6150,
    .nr_sectors = 1,
    .write_raw = thalion_c_write_raw,
    .read_raw = thalion_c_read_raw
};

static const uint32_t warp_crcs[] = {
    0xd7153107, 0x97350c47, 0x2b14ba93, 0x8e92a784, 0x63997b7d,
    0x63fe4f6b, 0xa9ff98f4, 0xdfbf54b6, 0x499f732a, 0xab265014,
    0xb15a33b1, 0x327d2f38, 0x12bab639, 0x00000000, 0xba6ec692,
    0x00000000, 0x41d5b860, 0x00000000, 0xce4e83be, 0x00000000,
    0x8b7f5a66, 0x8218f32d, 0x6d9e4d02
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
