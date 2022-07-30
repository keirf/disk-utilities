/*
 * disk/rhino.c
 * 
 * Custom format as used By the following games:
 * 
 * Borobodur
 * Bump'n'Burn
 * Hoi
 * Winter Games
 * 
 * Written in 2016/2022 by Keir Fraser/Keith Krellwitz. 
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x2291,0x2291
 *  u8  data[0x1810]      :: even block / odd block
 *  u8  padding
 *  u8  checksum
 * 
 * Checksum is eor'd bytes over the decoded data. 
 * 
 * Hoi: The track loaders on disk 1 calculates the checksum and stores it, 
 * then compare it against itself.  The disk 2 track loader decodes the
 * checksum and compares it correctly with the calculated checksum.
 * 
 * First data long contains header information (track number, disk identifier).
 * 
 * Winter Camp has a mastering error on disk 2 track 39.0.  Added a check for the
 * error and apply the fix.
 * 
 * TRKTYP_rhino data layout:
 *  u8 sector_data[0x1810]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rhino_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[(ti->len * 2) / 4], header;
        uint32_t idx_off = s->index_offset_bc - 31;
        uint8_t csum, chk[4], sum, raw[2];
        unsigned int i, fix = 0;
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
        if ((header & 0xfff0) != 0x3030 && (header & 0xffff) != 0x5256 
            && (header & 0xffff) != 0x5620)
            continue;

        /* Mastering error on track 39.0 of Winter Camp - check track and data*/
        if (be32toh(header) == 0x3130ff4e && dat[ti->len/4-1] == 0x55555555 
            && dat[96] == 0x6576656c) {
            for (i = 680; i < ti->len/4; i++)
                dat[i] = 0;
            fix = 1;
        }

        /* calculate checksum */
        for (i = sum = 0; i < ti->len/4; i++) {
            *(uint32_t*)&chk = dat[i];
            sum ^= chk[0] ^ chk[1] ^ chk[2] ^ chk[3];
        }

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;

        if (mfm_decode_word((uint16_t)s->word) != 0 && fix != 1)
            continue;

        /* checksum */
        if (stream_next_bytes(s, raw, 2) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &csum);
        if (fix == 1)
            csum = 0x24;

        if (csum != sum)
            continue;

        stream_next_index(s);
        if (s->track_len_bc > 103000)
            ti->total_bits = 105700;
        else if (s->track_len_bc > 101500)
            ti->total_bits = 102800;

        ti->data_bitoff = idx_off;
        set_all_sectors_valid(ti);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        return block;
    }

fail:
    return NULL;
}

static void rhino_read_raw(
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

struct track_handler rhino_handler = {
    .bytes_per_sector = 0x1810,
    .nr_sectors = 1,
    .write_raw = rhino_write_raw,
    .read_raw = rhino_read_raw
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
