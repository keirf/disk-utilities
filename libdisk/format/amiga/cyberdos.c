/*
 * disk/cyberdos.c
 *
 * Custom format as used on Double Dragon II by Virgin
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u16 0x5545 :: Sig
 *  u32 dat[(6144/4)/2] :: Fisrt 0xc00 of the data
 *  u32 0 :: psdding
 *  u16 0 :: psdding
 *  u32 dat[(6144/4)/2]  :: Second 0xc00 of the data
 *  u32 Checksum
 *
 * TRKTYP_cyberdos data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *cyberdos_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5545)
            continue;

        for (i = sum = 0; i < (ti->len/4)/2; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0)
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        for (i = 0; i < (ti->len/4)/2; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[(ti->len/4)/2+i]);
            sum += be32toh(dat[(ti->len/4)/2+i]);
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

static void cyberdos_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5545);

    for (i = sum = 0; i < (ti->len/4)/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = 0; i < (ti->len/4)/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[(ti->len/4)/2+i]));
        sum += be32toh(dat[(ti->len/4)/2+i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler cyberdos_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = cyberdos_write_raw,
    .read_raw = cyberdos_read_raw
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
