/*
 * disk/executioner.c
 *
 * Custom format as used on The Executioner by Tactix
 *
 * Written in 2023 by Keith Krellwitz
 *
 * TRKTYP_executioner_a data layout:
 *  u8 sector_data[11*512]
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x2aaaaaaa Padding
 *  u32 0xaaaaaaaa Padding
 *  u32 Checksum
 *  u32 dat[11*512]
 * 
 * 
 * TRKTYP_executioner_b data layout:
 *  u8 sector_data[11*512]
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x2aaaaaaa Padding
 *  u32 0xaaaaaaaa Padding
 *  u32 Length of next data chunk
 *  u32 Checksum
 *  u32 dat[11*512]
 * 
 * The checksum is the sum of the decoded data
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *executioner_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], raw2[ti->bytes_per_sector/4*2];
        uint32_t csum, sum;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x2aaaaaaa)
            continue;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* decode data for each sector */
        for (i = 0; i < ti->nr_sectors; i++) {
            if (stream_next_bytes(s, raw2, 2*ti->bytes_per_sector) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, raw2, &dat[i*(ti->bytes_per_sector/4)]);
        }

        /* validate checksum */
        for (i = sum =  0; i < ti->len/4; i++) {
            sum += be32toh(dat[i]);
        }
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

static void executioner_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    /* checksum*/
    for (i = sum = 0; i < ti->len/4; i++) {
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    /* data */
    for (i = 0; i < ti->nr_sectors; i++) {
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 512, &dat[i*(ti->bytes_per_sector/4)]);
    }
}

struct track_handler executioner_a_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = executioner_a_write_raw,
    .read_raw = executioner_a_read_raw
};

static void *executioner_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1], raw2[ti->bytes_per_sector/4*2], csum, sum, odd;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x2aaaaaaa)
            continue;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        /* checksum*/
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* length of data  */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &odd);

        /* decode data for each sector */
        for (i = 0; i < ti->nr_sectors; i++) {
            if (stream_next_bytes(s, raw2, 2*ti->bytes_per_sector) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, raw2, &dat[i*(ti->bytes_per_sector/4)]);
        }

        /* validate checksum */
        for (i = sum =  0; i < ti->len/4; i++) {
            sum += be32toh(dat[i]);
        }
        if (be32toh(csum) != sum)
            goto fail;

        dat[ti->len/4] = be32toh(odd);

        stream_next_index(s);
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void executioner_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    /* checksum */
    for (i = sum = 0; i < ti->len/4; i++) {
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    /* total length of next data chunk */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, dat[ti->len/4]);

    /* data */
    for (i = 0; i < ti->nr_sectors; i++) {
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 512, &dat[i*(ti->bytes_per_sector/4)]);
    }
}

struct track_handler executioner_b_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = executioner_b_write_raw,
    .read_raw = executioner_b_read_raw
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
