/*
 * disk/cyberworld.c
 *
 * Custom format as used on Cyber World by Magic Bytes and 
 * Subtrade: Return To Irata from boeder.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u16 0x2aaa 0x2aaa
 *  u32 dat[ti->len/4]
 *  u32 checksum
 *
 * TRKTYP_cyberworld data layout:
 *  u8 sector_data[5120]
 * 
 * TRKTYP_sub_trade_a data layout:
 *  u8 sector_data[6656]
 * 
 * TRKTYP_sub_trade_b data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *cyberworld_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

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

static void cyberworld_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
   tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler cyberworld_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = cyberworld_write_raw,
    .read_raw = cyberworld_read_raw
};

struct track_handler sub_trade_a_handler = {
    .bytes_per_sector = 6656,
    .nr_sectors = 1,
    .write_raw = cyberworld_write_raw,
    .read_raw = cyberworld_read_raw
};

struct track_handler sub_trade_b_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = cyberworld_write_raw,
    .read_raw = cyberworld_read_raw
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
