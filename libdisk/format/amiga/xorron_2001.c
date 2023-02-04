/*
 * disk/xorron_2001.c
 *
 * Custom format as used on Xorron 2001 by Magic Bytes
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u32 0x44894489 :: Sync
 *  u32 0x5a4f4d00 | tracknr :: header/track number
 *  u32 checksum
 *  u32 dat[ti->len/4]
 *
 * Checksum is the eor'd decoded data with a seed of 0x19981988
 * 
 * TRKTYP_xorron_2001 data layout:
 *  u8 sector_data[5920]
 * 
* TRKTYP_xorron_2001_short data layout:
 *  u8 sector_data[80]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *xorron_2001_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum, hdr;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 63;

        /* header/track number */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        if (be32toh(hdr) != (0x5a4f4d00 | tracknr))
            continue;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* data */
        sum = 0x19981988;
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(dat[i]);
        }

        if (be32toh(csum) != sum )
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

static void xorron_2001_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* header/track number */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0x5a4f4d00 | tracknr);

    /* checksum */
    sum = 0x19981988;
    for (i = 0; i < ti->len/4; i++) {
        sum ^= be32toh(dat[i]);
    }

    /* data */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler xorron_2001_handler = {
    .bytes_per_sector = 5920,
    .nr_sectors = 1,
    .write_raw = xorron_2001_write_raw,
    .read_raw = xorron_2001_read_raw
};

struct track_handler xorron_2001_short_handler = {
    .bytes_per_sector = 80,
    .nr_sectors = 1,
    .write_raw = xorron_2001_write_raw,
    .read_raw = xorron_2001_read_raw
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
