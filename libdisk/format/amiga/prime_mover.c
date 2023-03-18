/*
 * disk/prime_mover.c
 * 
 * Custom format as used by Prime Mover from Psygnosis
 * 
 * Written in 2023 Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x448a448aa :: Sync
 *  U32 0x55555555 :: Padding
 *  U16 Checksum - sum of the raw data
 *  U32 0xaaaaaaa5 :: Padding
 *  u16 data[6304]
 * 
 * TRKTYP_prime_mover data layout:
 *  u8 sector_data[6304]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *prime_mover_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint16_t raw[ti->len], dat[ti->len/2];
        uint16_t csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x448a448a)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;

        if (s->word != 0x55555555)
            continue;

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &csum);

        if (stream_next_bits(s, 32) == -1)
            goto fail;

        for (i = sum = 0; i < ti->len/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &dat[i]);
            sum += be16toh(raw[0]) + be16toh(raw[1]);
        }
    
        if (be16toh(csum) != sum)
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

static void prime_mover_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat, sum, raw[2];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x448a448a);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    raw[1] = 0x5555;
    for (i = sum = 0; i < ti->len/2; i++) {
       mfm_encode_bytes(bc_mfm_odd_even, 2, &dat[i], raw, be16toh(raw[1]));
       sum += be16toh(raw[0]) + be16toh(raw[1]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, sum);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaa5);

    for (i = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, be16toh(dat[i]));
    }
    
}

struct track_handler prime_mover_handler = {
    .bytes_per_sector = 6304,
    .nr_sectors = 1,
    .write_raw = prime_mover_write_raw,
    .read_raw = prime_mover_read_raw
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
