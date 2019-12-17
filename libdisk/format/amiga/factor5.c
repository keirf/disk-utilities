/*
 * disk/factor5.c
 *
 * Custom format as used on Turrican by Factor 5 / Rainbow Arts.
 *
 * Written in 2012 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u16 0x9521 :: Sync
 *  u16 0x2aaa
 *  u32 data[1630][2] :: bc_mfm_even_odd alternating longs
 *  u32 csum[2]   :: bc_mfm_even_odd
 * TRKTYP_turrican data layout:
 *  u8 sector_data[6552]
 *
 * TRKTYP_turrican_2 data layout:
 *  u8 sector_data[6832]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *turrican_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t csum, dat[2*ti->len/4];
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        for (i = csum = 0; i < ti->len/4; i++) {
            csum ^= be32toh(dat[2*i]) ^ be32toh(dat[2*i+1]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[2*i], &dat[i]);
        }
        csum &= 0x55555555u;

        if (stream_next_bytes(s, &dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[ti->len/4], &dat[ti->len/4]);
        if (csum != be32toh(dat[ti->len/4]))
            continue;

        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 110000) ? 111600 : 108000;
        
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void turrican_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        csum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 4, &dat[i]);
    }
    csum &= 0x55555555u;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler turrican_handler = {
    .bytes_per_sector = 6520,
    .nr_sectors = 1,
    .write_raw = turrican_write_raw,
    .read_raw = turrican_read_raw
};

struct track_handler turrican_2_handler = {
    .bytes_per_sector = 6800,
    .nr_sectors = 1,
    .write_raw = turrican_write_raw,
    .read_raw = turrican_read_raw
};

/*
 * Custom format as used on Turrican 3 by Factor 5 Rainbow Arts
 *
 * Written in 2019 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u16 0x2aa5
 *  u16 track number / 2
 *  u32 data[1644][2] :: bc_mfm_even_odd
 *  u32 csum[2]   :: bc_mfm_even_odd
 * TRKTYP_denaris_a data layout:
 *  u8 sector_data[6656]
 */

static void *turrican_3a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t sum, dat[ti->len/4], raw2[2];
        char *block;
        uint16_t trk, raw[2];

        if (s->word != 0x44892aa5)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &trk);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw2, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw2, &dat[i]);
            sum += be32toh(dat[i]);
        }
        if (stream_next_bytes(s, &dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[ti->len/4], &dat[ti->len/4]);
        if (sum != be32toh(dat[ti->len/4]))
            continue;

        stream_next_index(s);
        ti->total_bits = 111600;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void turrican_3a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44892aa5);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)tracknr/2);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}


struct track_handler turrican_3a_handler = {
    .bytes_per_sector = 6656,
    .nr_sectors = 1,
    .write_raw = turrican_3a_write_raw,
    .read_raw = turrican_3a_read_raw
};


/*
 * Custom format as used on Turrican 3 Track 0.1 by Factor 5 Rainbow Arts
 *
 * Written in 2019 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u16 0x2aaa (0x2aa5 for track 21.1)
 *  u32 data[1536][2] :: bc_mfm_even_odd
 *  u32 csum[2]   :: bc_mfm_even_odd
 * TRKTYP_denaris_a data layout:
 *  u8 sector_data[6144]
 */

static void *turrican_3b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t sum, dat[ti->len/4], raw2[2];
        uint16_t word_sync;
        char *block;

        if (ti->type == TRKTYP_turrican_3b)
            word_sync = 0x2aaa;
        else
            word_sync = 0x2aa5;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != word_sync)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw2, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw2, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, &dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[ti->len/4], &dat[ti->len/4]);
        if (sum != be32toh(dat[ti->len/4]))
            continue;
        
        stream_next_index(s);
        ti->total_bits = 100400;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void turrican_3b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    uint16_t word_sync;
    unsigned int i;

    if (ti->type == TRKTYP_turrican_3b)
        word_sync = 0x2aaa;
    else
        word_sync = 0x2aa5;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, word_sync);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}


struct track_handler turrican_3b_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = turrican_3b_write_raw,
    .read_raw = turrican_3b_read_raw
};

struct track_handler turrican_3c_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = turrican_3b_write_raw,
    .read_raw = turrican_3b_read_raw
};


/*
 * Custom format as used on Denaris by Factor 5 Rainbow Arts
 * and Hard Wired
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x9521 :: Sync
 *  u16 0x2aaa
 *  u32 csum[2]   :: bc_mfm_even_odd
 *  u32 data[1492][2] :: bc_mfm_even_odd
 * TRKTYP_denaris_a data layout:
 *  u8 sector_data[5968]
 */

static void *denaris_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t csum, sum, dat[2*ti->len/4];
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        csum = s->word & 0x55555555;

        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        for (i = sum = 0; i < ti->len/4; i++)
            sum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
        sum &= 0x55555555u;

        if (csum != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 99800;
        return block;
    }

fail:
    return NULL;
}

static void denaris_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

}

struct track_handler denaris_a_handler = {
    .bytes_per_sector = 5968,
    .nr_sectors = 1,
    .write_raw = denaris_a_write_raw,
    .read_raw = denaris_a_read_raw
};

/*
 * Custom format as used on Denaris by Factor 5 Rainbow Arts
 * and Hard Wired
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x9521 :: Sync
 *  u16 0x2aaa
 *  u32 data[1638][2] :: bc_mfm_even_odd alternating longs
 *  u32 csum[2]   :: bc_mfm_even_odd
 * TRKTYP_denaris_b data layout:
 *  u8 sector_data[6552]
 */

static void *denaris_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t csum, sum, raw[2], dat[ti->len/4];
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(raw[0]) ^  be32toh(raw[1]);
        }
        sum &= 0x55555555u;
        sum ^= 0xaaaaaaaau;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105200;
        return block;
    }

fail:
    return NULL;
}

static void denaris_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++){
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
    }
    csum &= 0x55555555u;
    csum ^= 0xaaaaaaaau;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler denaris_b_handler = {
    .bytes_per_sector = 6552,
    .nr_sectors = 1,
    .write_raw = denaris_b_write_raw,
    .read_raw = denaris_b_read_raw
};

/* TRKTYP_factor5_hiscore:
 *  u16 0x4489
 *  u16 0x2aaa
 *  u32 checksum[2]  :: even/odd mfm
 *  u32 data[99][2] :: even/odd mfm
 * Checksum is EOR data mfm longwords, AND 0x55555555, EOR 0x12345678
 *
 * Since the loader will handle a bad checksum, we tolerate this and create
 * a track containing just the 4489 sync word (avoids loader hang). */

static void *factor5_hiscore_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t sum, csum, dat[2];
        unsigned int i;

        if ((uint16_t)s->word != 0x4489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            continue;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, dat, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
        csum = be32toh(dat[0]) ^ 0x12345678;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, dat, 8) == -1)
                break;
            sum ^= be32toh(dat[0]) ^ be32toh(dat[1]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, dat, &block[i]);
        }
        sum &= 0x55555555;
        if (sum != csum) {
            trk_warn(ti, tracknr, "No saved high-score data found. "
                     "Creating empty track.");
            ti->nr_sectors = ti->bytes_per_sector = ti->len = 0;
        } else {
            set_all_sectors_valid(ti);
        }
        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 102000) ? 102500
            : 100150;

        return block;
    }

    memfree(block);
    return NULL;
}

static void factor5_hiscore_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    if (ti->len == 0)
        return;

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]) ^ be32toh(dat[i] >> 1);
    csum &= 0x55555555;
    csum ^= 0x12345678;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler factor5_hiscore_handler = {
    .bytes_per_sector = 396,
    .nr_sectors = 1,
    .write_raw = factor5_hiscore_write_raw,
    .read_raw = factor5_hiscore_read_raw
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
