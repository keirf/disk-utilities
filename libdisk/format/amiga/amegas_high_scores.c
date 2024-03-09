/*
 * disk/amegas_high_scores.c
 *
 * Custom format as used on Amegas by reLINE.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u16 0x2aaa 0x2aaa
 *  u32 dat[264/4]
 *  u32 checksum (##202020) - checksum itself is only a u8
 * 
 * The checksum is calculated by eor.b the dcoded data
 *
 * TRKTYP_amegas data layout:
 *  u8 sector_data[264]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint8_t checksum(uint32_t *dat, uint32_t data_length)
{
    unsigned int i;
    uint8_t sum;

    for (i = sum = 0; i < data_length/4; i++) {
        sum ^= (uint8_t)(be32toh(dat[i]) >> 24);
        sum ^= (uint8_t)(be32toh(dat[i]) >> 16);
        sum ^= (uint8_t)(be32toh(dat[i]) >> 8);
        sum ^= (uint8_t)be32toh(dat[i]);
    }
    return sum;
}

static void *amegas_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1], csum;
        uint8_t sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }
        sum = checksum(dat,ti->len);

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);


        if ((uint8_t)(be32toh(csum) >> 24) != sum)
            continue;

        dat[ti->len/4] = be32toh(csum);

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

static void amegas_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
    sum = checksum(dat,ti->len);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (sum << 24) | dat[ti->len/4]);
}

struct track_handler amegas_high_scores_handler = {
    .bytes_per_sector = 264,
    .nr_sectors = 1,
    .write_raw = amegas_write_raw,
    .read_raw = amegas_read_raw
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
