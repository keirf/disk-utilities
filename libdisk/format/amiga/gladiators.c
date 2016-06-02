/*
 * disk/gladiators.c
 * 
 * Custom format as used on Gladiators by Smash 16.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8915 :: Sync
 *  u32 dat[1536][2] :: Interleaved even/odd words
 *  u32 csum[2] :: Even/odd words, ADD.L sum over raw MFM data
 * 
 * TRKTYP_gladiators data layout:
 *  u8 sector_data[6*1024]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *gladiators_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[1536], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x8915)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(raw[0]) + be32toh(raw[1]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
        if (csum != be32toh(sum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void gladiators_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat, raw[2];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8915);

    raw[1] = htobe32(0x8915); /* get 1st clock bit right for checksum */
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[i], raw, be32toh(raw[1]));
        csum += be32toh(raw[0]) + be32toh(raw[1]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler gladiators_handler = {
    .bytes_per_sector = 6*1024,
    .nr_sectors = 1,
    .write_raw = gladiators_write_raw,
    .read_raw = gladiators_read_raw
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
