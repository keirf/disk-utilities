/*
 * disk/prehistoric_tale.c
 *
 * Custom format as used on A Prehistoric Tale by Thalion.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u16 0x44a9 0x44a9 0x44a9 :: padding
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

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44a944a9)
            continue;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x44a9)
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
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44a944a9);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x44a9);

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
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
