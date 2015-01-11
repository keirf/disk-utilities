/*
 * disk/eye_of_horus.c
 * 
 * Custom format as used on Eye Of Horus by Logotron / Denton Designs.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u32 header[5][2]  :: Interleaved even/odd longs
 *  u32 header_csum[2]
 *  u32 data_csum[2]
 *  u32 data[N][2]
 * 
 * TRKTYP_eye_of_horus data layout:
 *  u8 sector_data[N]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *eye_of_horus_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], hdr[7], dat[0x1600/4], longs_per_sector;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < ARRAY_SIZE(hdr); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr[i]);
        }
        if ((be32toh(hdr[0]) != (0xff00000b | (tracknr << 16))) ||
            (be32toh(hdr[1]) > 0x1600) ||
            (be32toh(hdr[5]) != amigados_checksum(hdr, 5*4)))
            continue;

        ti->bytes_per_sector = be32toh(hdr[1]);
        longs_per_sector = ti->bytes_per_sector / 4;
        ti->len = (longs_per_sector + 3) * 4;

        for (i = 0; i < longs_per_sector; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }
        if (be32toh(hdr[6]) != amigados_checksum(dat, longs_per_sector*4))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, longs_per_sector*4);
        memcpy(&block[longs_per_sector*4], &hdr[2], 3*4);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void eye_of_horus_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, hdr[7];
    unsigned int i, longs_per_sector = ti->bytes_per_sector/4;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    hdr[0] = htobe32(0xff00000b | (tracknr << 16));
    hdr[1] = htobe32(ti->bytes_per_sector);
    memcpy(&hdr[2], &dat[longs_per_sector], 3*4);
    hdr[5] = htobe32(amigados_checksum(hdr, 5*4));
    hdr[6] = htobe32(amigados_checksum(dat, longs_per_sector*4));

    for (i = 0; i < ARRAY_SIZE(hdr); i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(hdr[i]));

    for (i = 0; i < longs_per_sector; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

    /* Trailing zeros to ensure correct data checksum for data lengths
     * which are not a multiple of 4. These may be a mastering error: 
     * the remaindered bytes are included in the checksum calculation but 
     * are not decoded and used. They are always zero. */
    if (ti->bytes_per_sector & 3)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
}

struct track_handler eye_of_horus_handler = {
    .nr_sectors = 1,
    .write_raw = eye_of_horus_write_raw,
    .read_raw = eye_of_horus_read_raw
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
