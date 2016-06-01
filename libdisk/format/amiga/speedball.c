/*
 * disk/speedball.c
 * 
 * Custom format as used on Speedball by The Bitmap Brothers / Image Works.
 * 
 * Written in 2016 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489
 *  u32 0x5554,0x5554 (0xfefe)
 *  u32 'THBB'/0x54484242 :: even/odd
 *  u32 track_len (5952)  :: even/odd
 *  u32 checksum          :: even/odd
 *  u32 dat[1000]         :: even/odd
 *  Checksum is EOR.Lsum of all decoded data longs
 * 
 * TRKTYP_speedball data layout:
 *  u8 sector_data[5952]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define ID_THBB 0x54484242

static void *speedball_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[10000], track_len, csum;
        uint32_t idx_off = s->index_offset_bc - 31;
        unsigned int i;
        void *block;

        if (s->word != 0x44894489)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0xfefe)
            continue;

        if (stream_next_bytes(s, dat, 3*8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[0], &dat[0]);
        if (be32toh(dat[0]) != ID_THBB)
            continue;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[2], &dat[2]);
        track_len = be32toh(dat[2]);
        if (track_len != 5952) /* track length is always 5952 */
            continue;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[4], &dat[4]);
        csum = be32toh(dat[4]);

        if (stream_next_bytes(s, dat, track_len*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, track_len, dat, dat);

        for (i = 0; i < track_len / 4; i++)
            csum ^= be32toh(dat[i]);
        if (csum != 0)
            continue;

        ti->data_bitoff = idx_off;
        set_all_sectors_valid(ti);
        ti->bytes_per_sector = ti->len = track_len;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        return block;
    }

fail:
    return NULL;
}

static void speedball_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum = 0, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xfefe);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, ID_THBB);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, ti->len);

    for (i = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler speedball_handler = {
    .nr_sectors = 1,
    .write_raw = speedball_write_raw,
    .read_raw = speedball_read_raw
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
