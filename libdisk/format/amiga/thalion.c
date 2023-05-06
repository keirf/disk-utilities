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
        
        if (tracknr != 30 && tracknr != 158 && tracknr != 1 && tracknr != 137)
            goto fail;

        if (ti->type == TRKTYP_seven_gates_of_jambala) {
            if (tracknr == 30 && sum != 0x48f0dfdc)
                continue;
            else if (tracknr == 158 && sum != 0x35d08d19 && sum != 0x32a637b6)
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
 * Standard CRC16 checksum which should equal 0xbc85
 * 
 * TRKTYP_warp_a data layout:
 *  u8 sector_data[6150]
 */

static void *warp_a_write_raw(
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
        stream_start_crc(s);
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (s->crc16_ccitt != 0xbc85)
            continue;

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

static void warp_a_read_raw(
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
    .write_raw = warp_a_write_raw,
    .read_raw = warp_a_read_raw
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
 * Standard crc16 checksum
 * 
 * TRKTYP_warp_b data layout:
 *  u8 sector_data[6150]
 */

static void *warp_b_write_raw(
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

        stream_start_crc(s);
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

        if (s->crc16_ccitt != 0)
            continue;

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

static void warp_b_read_raw(
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
    .write_raw = warp_b_write_raw,
    .read_raw = warp_b_read_raw
};


/*
 * Custom format as used on A Prehistoric Tale by Thalion.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u8  0xa1,0xa1,0xa1 :: padding
 *  u32 tracknr/2
 *  u32 dat[6144/4]
 *  u32 checksum
 *
 * The checksum is eor'd over the decoded data, tracknr/2 and 
 * the seed (0x4a4f4348)
 * 
 * TRKTYP_prehistoric_tale data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SEED 0x4a4f4348; /* JOCH */

static void *prehistoric_tale_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum, trk;
        unsigned int i;
        char *block;

        /* sync + padding */
        if (s->word != 0x448944a9)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0xa1a1)
            continue;

        /* track number / 2 */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);

        if (tracknr/2 != be32toh(trk))
            continue;
        sum = be32toh(trk) ^ SEED;

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
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

static void prehistoric_tale_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 24, 0xa1a1a1);

    /* track number / 2 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, tracknr/2);
    sum = (tracknr/2) ^ SEED;

    /* data */
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum ^= be32toh(dat[i]);
    }

    /* checksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler prehistoric_tale_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = prehistoric_tale_write_raw,
    .read_raw = prehistoric_tale_read_raw
};


/*
 * Custom format as used on A Leavin's Teramis by Thalion.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 
 * Protection
 *  u16 0x5224 :: Sync
 *  u8  0xa1   :: Padding
 *  weak bits
 * 
 * The protection appears to be on all tracks, but only seen the check
 * against track 64
 * 
 * Data
 *  u16 0x4489 :: Sync
 *  u8  0xa1,0xa1,0xa1 :: padding
 *  u32 tracknr/2
 *  u32 dat[6016/4]
 *  u32 checksum
 *
 * The checksum is eor'd over the decoded data, tracknr/2 and 
 * the seed (0x4a4f4348)
 * 
 * TRKTYP_leavin_teramis_a data layout:
 *  u8 sector_data[6016]
 */

static void *leavin_teramis_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    /* check for presence of the protection */
    while (stream_next_bit(s) != -1) {
        if (s->word == 0x522444a9)
            break;
    }

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum, trk;
        unsigned int i;
        char *block;

        /* sync + padding */
        if (s->word != 0x448944a9)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0xa1a1)
            continue;

        /* track number / 2 */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);

        if (tracknr/2 != be32toh(trk))
            continue;
        sum = be32toh(trk) ^ SEED;

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
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

static void leavin_teramis_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* weak bit protection appears on all tracks */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5224);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xa1);
    tbuf_weak(tbuf, 64);

    /* small gap */
    for (i = 0; i < 4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
    }

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 24, 0xa1a1a1);

    /* track number / 2 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, tracknr/2);
    sum = (tracknr/2) ^ SEED;

    /* data */
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum ^= be32toh(dat[i]);
    }

    /* checksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}


struct track_handler leavin_teramis_a_handler = {
    .bytes_per_sector = 6016,
    .nr_sectors = 1,
    .write_raw = leavin_teramis_a_write_raw,
    .read_raw = leavin_teramis_a_read_raw
};


/*
 * Custom format as used on Leavin' Teramis from Thalion.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x5224 0x5224 0x5224 :: Sync
 *  u32 dat[ti->len/4]
 *
 * There is a form of checksum, I calculated checksums and 
 * verified with the official IPF and use crc16_ccitt calculation
 * which equals 0x759d for each track
 * 
 *
 * TRKTYP_leavin_teramis_b data layout:
 *  u8 sector_data[6160]
 */

static const uint32_t teramis_b_crcs[];

static void *leavin_teramis_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x52245224)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        stream_start_crc(s);
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (tracknr < 8 || tracknr > 26 || tracknr % 2 == 1)
            continue;
       
        if (sum != teramis_b_crcs[(tracknr-8)/2] || s->crc16_ccitt != 0x759d)
            continue;

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

static void leavin_teramis_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52245224);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

}

struct track_handler leavin_teramis_b_handler = {
    .bytes_per_sector = 6160,
    .nr_sectors = 1,
    .write_raw = leavin_teramis_b_write_raw,
    .read_raw = leavin_teramis_b_read_raw
};


static const uint32_t teramis_b_crcs[] = {
    0xdc93c115, 0xf9dbf5d8, 0xd10885ce, 0x465ce946, 0xb3e22e56,
    0x4ac8192a, 0xf2a17022, 0x1542f339, 0x62fe80bb, 0x5a560999
};


/*
 * Custom format as used on Leavin' Teramis High Scores.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 
 *  u16 0x4489 0x4489 0x4489 0x5554 :: Sync
 *  u32 dat[2]
 *  u16 0x5555 x 20 :: padding
 * 
 *  u16 0x4489 0x4489 0x4489 0x5545 :: Sync
 *  u32 dat[2572/4]
 *
 * Checksum for the header and data are standard crc16_ccitt
 * checksums.
 * 
 * TRKTYP_leavin_teramis_high data layout:
 *  u8 sector_data[2572+8]
 */

static void *leavin_teramis_high_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t raw[2], dat[ti->len/4];
    unsigned int i;
    char *block;

    while (stream_next_bit(s) != -1) {

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        stream_start_crc(s);
        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44895554)
            continue;

        for (i = 0; i < 2; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if(s->crc16_ccitt == 0)
            break;

    }


    while (stream_next_bit(s) != -1) {

        /* sync */
        if (s->word != 0x44894489)
            continue;

        stream_start_crc(s);
        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44895545)
            continue;

        /* data */
        for (i = 2; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if(s->crc16_ccitt != 0)
            continue;

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

static void leavin_teramis_high_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895554);

    /* data */
    for (i = 0; i < 2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

    /* padding */
    for (i = 0; i < 20; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);
    }

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895545);

    /* data */
    for (i = 2; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}


struct track_handler leavin_teramis_high_handler = {
    .bytes_per_sector = 2572+8,
    .nr_sectors = 1,
    .write_raw = leavin_teramis_high_write_raw,
    .read_raw = leavin_teramis_high_read_raw
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
