/*
 * disk/power_drift.c
 *
 * Custom format as used on Power Drift by Activision
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u16 0x2aaa
 *  u32 dat[ti->len/4]
 * 
 * dat[0] contains the tracknr/2
 * 
 * Checksum is stored in dat[ti->len/4-1] and the
 * checksum is the sum of the decoded data.
 *
 * TRKTYP_power_drift_loader layout:
 *  u8 sector_data[5120]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *power_drift_loader_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4], sum;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;       
        mfm_decode_bytes(bc_mfm_odd_even, ti->len, dat, dat);

        /* track number divided by 2 */
        if (be32toh(dat[0]) != tracknr/2)
            continue;

        /* calculate checksum */
        for (i = sum = 0; i < ti->len/4-1; i++) {
            sum += be32toh(dat[i]);
        }

        if (be32toh(dat[ti->len/4-1]) != sum)
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

static void power_drift_loader_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    /* Set track number divided by 2 */
    dat[0] = htobe32(tracknr/2);

    /* calculate checksum */
    for (i = sum = 0; i < ti->len/4-1; i++) {
        sum += be32toh(dat[i]);
    }
    dat[ti->len/4-1] = htobe32(sum);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
}

struct track_handler power_drift_loader_handler = {
    .bytes_per_sector = 4104,
    .nr_sectors = 1,
    .write_raw = power_drift_loader_write_raw,
    .read_raw = power_drift_loader_read_raw
};


/*
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x2aaaaaaa
 *  u16 8 X u16 Weak bits
 * 
 *  u32 0x44894489 Sync
 *  u16 0x2aaa
 *  u32 dat[ti->len/4]
 * 
 * dat[0] contains the tracknr/2
 * 
 * After mfm decoding, the data is then derypted and the
 * checksum is calculated by the sum of the decrypted 
 * data.
 *
 * TRKTYP_power_drift layout:
 *  u8 sector_data[5640]
 * 
 */

static void *power_drift_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word == 0x2aaaaaaa)
            break;
    }

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4], sum, seed;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, ti->len, dat, dat);

        /* track number divided by 2 */
        if (be32toh(dat[0]) != tracknr/2)
            continue;

        /* decrypt data */
        seed = 0xdec0ded1;
        for (i = 1; i < ti->len/4; i++) {
            seed ^= be32toh(dat[i]);
            dat[i] = htobe32(seed);
        }

        /* calculate checksum */
        for (i = sum = 0; i < ti->len/4-1; i++) {
            sum += be32toh(dat[i]);
        }

        if (be32toh(dat[ti->len/4-1]) != sum)
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

static void power_drift_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, seed, nxt_seed, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    /* Weak bits */
    tbuf_weak(tbuf, 48);
    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    /* Set track number divided by 2 */
    dat[0] = htobe32(tracknr/2);
    /* calculate checksum */
    for (i = sum = 0; i < ti->len/4-1; i++) {
        sum += be32toh(dat[i]);
    }
    dat[ti->len/4-1] = htobe32(sum);

    /* encrypt data */
    seed = 0xdec0ded1;
    for (i = 1; i < ti->len/4; i++) {
        nxt_seed = be32toh(dat[i]);
        dat[i] ^= htobe32(seed);
        seed = nxt_seed;
    }

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
}

struct track_handler power_drift_handler = {
    .bytes_per_sector = 5640,
    .nr_sectors = 1,
    .write_raw = power_drift_write_raw,
    .read_raw = power_drift_read_raw
};

/*
 *
 * RAW TRACK LAYOUT:
 * 11 sectors back-to-back:
 *  u32 0x44894489 Sync
 *  u16 0x2aaa
 *  u32 dat[520/4]
 *  u16 0x2aaa
 *  u32 0
 * 
 * dat[0] contains the track number and sector number
 * 
 * Checksum is stored in dat[520/4-1] for each sector and the
 * checksum is the sum of the decoded data.
 *
 * TRKTYP_power_drift_disk_2 layout:
 *  u8 sector_data[11][520]
 * 
 */

static void *power_drift_disk_2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
   struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int nr_valid_blocks = 0, sec, i;

    block = memalloc(ti->nr_sectors*ti->bytes_per_sector);
    ti->data_bitoff = s->index_offset_bc - 31;
    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t dat[2*ti->bytes_per_sector/4], sum;

        /* sync */
        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        if (stream_next_bytes(s, dat, 2*ti->bytes_per_sector) == -1)
            break;       
        mfm_decode_bytes(bc_mfm_odd_even, ti->bytes_per_sector, dat, dat);

        for (i = sum = 0; i < ti->bytes_per_sector/4-1; i++) {
            sum += be32toh(dat[i]);
        }

        /* track number check */
        if ((uint8_t)(be32toh(dat[0])>>8) != tracknr)
            continue;

        /* sector */
        sec = (uint8_t)be32toh(dat[0]);

        if (be32toh(dat[ti->bytes_per_sector/4-1]) != sum)
           continue;

        memcpy(&block[sec*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        sec += 1;
    }
    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void power_drift_disk_2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector/4], sum;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        /* padding */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

        memcpy(dat, &ti->dat[sec*ti->bytes_per_sector], ti->bytes_per_sector);

        dat[0] = htobe32(((tracknr)<<8) | sec);

        /* calculate checksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4-1; i++) {
            sum += be32toh(dat[i]);
        }
        dat[ti->bytes_per_sector/4-1] = htobe32(sum);

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->bytes_per_sector, dat);

        /* padding */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
    }
}

struct track_handler power_drift_disk_2_handler = {
    .bytes_per_sector = 520,
    .nr_sectors = 11,
    .write_raw = power_drift_disk_2_write_raw,
    .read_raw = power_drift_disk_2_read_raw
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
