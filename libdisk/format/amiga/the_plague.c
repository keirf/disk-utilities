/*
 * disk/the_plague.c
 *
 * Custom format as used by The Plague from Innerprise
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYPE_the_plague_a
 * 
 *  u32 0x12251225, 0x12291229, 0x22442244, 0x44894489, 
 *      0x448a448a, 0x44a244a2, 0x89448944, 0xa244a244,
 *      0x8a448a44 ::  Sync (each track can be a 1 of 9 syncs)
 *  u16 0 : pad
 *  u32 dat[6144/4]
 *  checksum : sum of decoded data
 * 
 * TRKTYPE_the_plague_b
 * 
 *  u32 0xfaf3faf3 ::  Sync
 *  u16 0 : pad
 *  u32 dat[3072/4]
 *  checksum : sum of decoded data
 * 
 *  In order to read and write the data for TRKTYPE_the_plague_b
 *  it required setting GCR 4us bit time.  
 *  
 */

#include <libdisk/util.h>
#include <private/disk.h>

const static uint32_t syncs[] = {
    0x12251225, 0x12291229, 0x22442244, 0x44894489, 
    0x448a448a, 0x44a244a2, 0x89448944, 0xa244a244,
    0x8a448a44
};

static void *the_plague_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t sync;
    uint32_t raw[2], dat[ti->len/4+1], csum, sum;
    uint16_t raw16[2], pad;
    unsigned int i, k;
    char *block;

    for (k = 0; k < ARRAY_SIZE(syncs); k++) {

        sync = syncs[k];
        while (stream_next_bit(s) != -1) {

            /* sync */
            if (s->word != sync)
                continue;
            ti->data_bitoff = s->index_offset_bc - 32;

            /* pad */
            if (stream_next_bytes(s, raw16, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw16, &pad);
            if (pad != 0)
                break;

            /* data */
            for (i = sum = 0; i < ti->len/4; i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    goto fail;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
                sum += be32toh(dat[i]);
            }

            /* checksum */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            if (be32toh(csum) != sum)
                break;

            dat[ti->len/4] = sync;

            block = memalloc(ti->len+4);
            memcpy(block, dat, ti->len+4);
            set_all_sectors_valid(ti);
            return block;
        }
        stream_reset(s);
    }

fail:
    return NULL;
}

static void the_plague_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->len/4]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler the_plague_a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = the_plague_a_write_raw,
    .read_raw = the_plague_a_read_raw
};


static void *the_plague_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t raw[2], dat[ti->len/4], csum, sum;
    uint16_t raw16[2], pad;
    unsigned int i;
    char *block;

    /* GCR 4us bit time */
    stream_set_density(s, 4000);

    while (stream_next_bit(s) != -1) {

        if (s->word != 0xfaf3faf3)
            continue;

        ti->data_bitoff = s->index_offset_bc - 32;

        /* pad */
        if (stream_next_bytes(s, raw16, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw16, &pad);
        if (pad != 0)
            break;

        /* data */
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            break;

        ti->total_bits = 50500;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void the_plague_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xfaf3faf3);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler the_plague_b_handler = {
    .bytes_per_sector = 3072,
    .nr_sectors = 1,
    .write_raw = the_plague_b_write_raw,
    .read_raw = the_plague_b_read_raw
};


static int check_sequence(struct stream *s, unsigned int nr, uint16_t word)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint16_t)(s->word) != word)
            break;
    }
    return !nr;
}

static int check_length(struct stream *s, unsigned int min_bits)
{
    stream_next_index(s);
    return (s->track_len_bc >= min_bits);
}

/* TRKTYP_the_plague_c:
 *
 *  Track 0.1 on disk 1
 *  u32 0xa244a244 :: MFM sync
 *  u16 0xa244 (6260 times, = 100173 MFM bits)
 *
 * Only checks to see if 0xa244 is located at 16 byte into 
 * the raw data buffer
 */

static void *the_plague_c_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (s->word != 0xa244a244)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (!check_sequence(s, 6000, 0xa244))
            continue;

        if (!check_length(s, 99000))
            break;

        ti->total_bits = 100200;
        return memalloc(0);
    }

    return NULL;
}

static void the_plague_c_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa244a244);
    for (i = 0; i < 6260; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa244);
    }
}

struct track_handler the_plague_c_handler = {
    .write_raw = the_plague_c_write_raw,
    .read_raw = the_plague_c_read_raw
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
