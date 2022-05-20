/*
 * disk/apidya.c
 *
 * Custom format as used on Apidya by Play Byte.
 *
 * Written in 2022 by Keith Krellwitz 
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYP_apidya_1
 *  u16 (4489 Sync)
 *  u16 padding : 0x2aaa
 *  u32 dat[6144/4]
 *  u32 Checksum
 * 
 * TRKTYP_apidya_b
 *  u16 (4489 Sync)
 *  u16 padding : 0x2aa9
 *  u16 Track Number / 2
 *  u32 dat[6656/4]
 *  u32 Checksum
 * 
 * TRKTYP_apidya_c
 *  u16 (4489 Sync)
 *  u16 padding : 0x2aa9
 *  u32 dat[6144/4]
 *  u32 Checksum
 *
 * TRKTYP_apidya_c
 *  u16 (4489 Sync)
 *  u16 padding : 0x4aa9
 *  u16 Track Number / 2
 *  u32 dat[6656/4]
 *  u32 Checksum
 * 
 * Checksum: The sum of decoded data
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct apidya_info {
    uint16_t pad;
};

static void *apidya_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct apidya_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        uint16_t raw16[2], trk;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* pad */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != info->pad)
            continue;

        /* track number/2 only for track type TRKTYP_apidya_b & TRKTYP_apidya_d*/
        if (ti->type == TRKTYP_apidya_b || ti->type == TRKTYP_apidya_d) {
            if (stream_next_bytes(s, raw16, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw16, &trk);
            if (be16toh(trk) != tracknr/2)
                continue;
        }

        /* data */
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            continue;

        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 102200) ? 111500 : 100400;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void apidya_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct apidya_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->pad);
    if (ti->type == TRKTYP_apidya_b || ti->type == TRKTYP_apidya_d) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, tracknr/2);    
    }

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler apidya_a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = apidya_write_raw,
    .read_raw = apidya_read_raw,
    .extra_data = & (struct apidya_info) {
        .pad = 0x2aaa}
};

struct track_handler apidya_b_handler = {
    .bytes_per_sector = 6656,
    .nr_sectors = 1,
    .write_raw = apidya_write_raw,
    .read_raw = apidya_read_raw,
    .extra_data = & (struct apidya_info) {
        .pad = 0x2aa9}
};

struct track_handler apidya_c_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = apidya_write_raw,
    .read_raw = apidya_read_raw,
    .extra_data = & (struct apidya_info) {
        .pad = 0x2aa9}
};

struct track_handler apidya_d_handler = {
    .bytes_per_sector = 6656,
    .nr_sectors = 1,
    .write_raw = apidya_write_raw,
    .read_raw = apidya_read_raw,
    .extra_data = & (struct apidya_info) {
        .pad = 0x4aa9}
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
