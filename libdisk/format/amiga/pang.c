/*
 * disk/pang.c
 *
 * Custom format as used on Pang by Ocean.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync (TRKTYP_pang_b)
 *  u16 0x5041, 0x0000 : (PA_SIG | (uint16_t)tracknr/2 << 8) | ((tracknr % 2 == 0) ? 0xff : 0) 
 *  u32 dat[6304/4]
 *  u32 checksum
 *
 *  u16 0x4124 Sync (TRKTYP_pang_a)
 *  u16 0x5041, 0x0000 : (PA_SIG | (uint16_t)tracknr/2 << 8) | ((tracknr % 2 == 0) ? 0xff : 0)
 *  u32 dat[6304/4]
 *  u32 checksum
 *
 * The checksum is the sum over the raw data & 0x555555555 including the SIG data
 * 
 * TRKTYP_pang_a data layout:
 *  u8 sector_data[6304]
 * 
 * TRKTYP_pang_b data layout:
 *  u8 sector_data[6304]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define PA_SIG 0x50410000

struct pang_info {
    uint16_t sync;
};

static void *pang_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct pang_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum, hdr, chdr;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != info->sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &hdr);

        sum = (be32toh(raw[0]) & 0x55555555u) + (be32toh(raw[1]) & 0x55555555u);
        chdr = (PA_SIG | (uint16_t)tracknr/2 << 8) | ((tracknr % 2 == 0) ? 0xff : 0);
        if (be32toh(hdr) != chdr)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum += (be32toh(raw[0]) & 0x55555555u) + (be32toh(raw[1]) & 0x55555555u);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);
        if (be32toh(csum) != sum)
            goto fail;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void pang_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct pang_info *info = handlers[ti->type]->extra_data;

    uint32_t *dat = (uint32_t *)ti->dat, sum, hdr;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);
    hdr = (PA_SIG | (uint16_t)tracknr/2 << 8) | ((tracknr % 2 == 0) ? 0xff : 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, hdr);

    sum = (hdr & 0x55555555u) + ((hdr >> 1) & 0x55555555u);
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        sum += (be32toh(dat[i]) & 0x55555555u) + ((be32toh(dat[i]) >> 1) & 0x55555555u);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, sum);
}

struct track_handler pang_a_handler = {
    .bytes_per_sector = 6304,
    .nr_sectors = 1,
    .write_raw = pang_write_raw,
    .read_raw = pang_read_raw,
    .extra_data = & (struct pang_info) {
        .sync = 0x4124}
};

struct track_handler pang_b_handler = {
    .bytes_per_sector = 6304,
    .nr_sectors = 1,
    .write_raw = pang_write_raw,
    .read_raw = pang_read_raw,
    .extra_data = & (struct pang_info) {
        .sync = 0x4489}
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
