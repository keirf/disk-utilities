/*
 * disk/sliding_skill.c
 *
 * Custom format as used Sliding Skill by Funworld
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0xFF##0001 where ## is the track number
 *  U32x5 header data
 *  u32 checksum
 *  u32 dat[ti->len/4]
 *
 * The checksum is the raw data u32 & 0x55555555 and xor'd
 * with the previous u32 and then the result is anded 
 * with 0x55555555
 * 
 * 
 * TRKTYP_sliding_skill data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sliding_skill_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+5], csum, sum, hdr;
        unsigned int i;
        char *block;

        // sync
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        // track number
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        if ((uint8_t)(be32toh(hdr) >> 24) != 0xff)
            continue;

        if ((uint8_t)(be32toh(hdr) >> 16) != tracknr)
            continue;

        // header
        for (i = 0; i < 5; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i+ti->len/4]);
        }

        // checksum
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        // data
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= be32toh(raw[0]) & 0x55555555;
            sum ^= be32toh(raw[1]) & 0x55555555;
        }
        sum &= 0x55555555;

        if (be32toh(csum) != sum)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len+20);
        memcpy(block, dat, ti->len+20);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void sliding_skill_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum, hdr;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    hdr = 0xff000001u | tracknr << 16;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    for (i = 0; i < 5; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, be32toh(dat[i+ti->len/4]));

    for (i = sum = 0; i < ti->len/4; i++) {
        sum ^= be32toh(dat[i]);
    }

    for (i = sum = 0; i < ti->len/4; i++)
        sum ^= be32toh(dat[i]) ^ (be32toh(dat[i]) >> 1);
    sum &= 0x55555555u;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler sliding_skill_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = sliding_skill_write_raw,
    .read_raw = sliding_skill_read_raw
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
