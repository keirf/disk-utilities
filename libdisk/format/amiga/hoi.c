/*
 * disk/hoi.c
 * 
 * Custom format as used on Hoi by Hollyware.
 * 
 * Written in 2022 by Keith Krellwitz. This is based on
 * Keir Fraser's Bump N Burn decoder
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x2291,0x2291
 *  u8  data[0x1810]      :: even block / odd block
 *  u8  padding
 *  u8  checksum
 * 
 * Checksum is eor'd over the decoded data. The track loaders on disk 1
 * calculates the checksum and store it them compare it against itself.  The
 * disk 2 track loader decodes the checksum and compares it correctly with
 * the calclulated checksum.
 * 
 * First data long contains header information (track number, disk identifier).
 * 
 * TRKTYP_hoi data layout:
 *  u8 sector_data[0x1810]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *hoi_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[(ti->len * 2) / 4], header;
        uint32_t idx_off = s->index_offset_bc - 31;
        uint8_t csum, chk[4], sum, raw[2];
        unsigned int i;
        void *block;

        /* sync */
        if (s->word != 0x22912291)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        /* header */
        header = be32toh(dat[0]);
        if ((header >> 24) != tracknr)
            continue;

        /* check disk identifier */
        if ((header & 0xffff) != 0x5256 && (header & 0xffff) != 0x5620)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            *(uint32_t*)&chk = dat[i];
            sum ^= chk[0] ^ chk[1] ^ chk[2] ^ chk[3];
        }

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;

        /* checksum */
        if (stream_next_bytes(s, raw, 2) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &csum);
        if (csum != sum)
            continue;

        stream_next_index(s);
        if (s->track_len_bc > 101500)
            ti->total_bits = 105700;

        ti->data_bitoff = idx_off;
        set_all_sectors_valid(ti);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        return block;
    }

fail:
    return NULL;
}

static void hoi_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;
    uint8_t sum;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22912291);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, ti->dat);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    /* checksum */
    for (i = sum = 0; i < ti->len; i++) {
        sum ^= ti->dat[i];
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, sum);
}

struct track_handler hoi_handler = {
    .bytes_per_sector = 0x1810,
    .nr_sectors = 1,
    .write_raw = hoi_write_raw,
    .read_raw = hoi_read_raw
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
