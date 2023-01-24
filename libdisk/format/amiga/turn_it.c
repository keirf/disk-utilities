/*
 * disk/turn_it.c
 *
 * Custom format as used on Turn It by Kingsoft
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 0xaaaaaaaa
 *  u32 dat[ti->len/4]
 *
 * The tracks do not contain a checksum. I have create my own checksum
 * to validate the game
 *
 * TRKTYP_turn_it data layout:
 *  u8 sector_data[6300]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint16_t crcs[];

static void *turn_it_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (crcs[tracknr] != s->crc16_ccitt)
            continue;

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

static void turn_it_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler turn_it_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = turn_it_write_raw,
    .read_raw = turn_it_read_raw
};

static const uint16_t crcs[] = {
    0x0000, 0xfb64, 0x74c1, 0xb2e8, 0x4115, 0x450d, 0x90f8, 0xfebc, 0xe748, 0xa3b2,
    0xe1c6, 0x4125, 0xce58, 0xfbf6, 0x7d4a, 0xb1fb, 0xbd34, 0x4012, 0x6c01, 0xff00,
    0x64a7, 0xda71, 0xf398, 0x5460, 0xba96, 0x0a44, 0x80e8, 0x0fa5, 0x7032, 0x4d72,
    0x0445, 0xc1b1, 0xc367, 0x5daa, 0x1aa6, 0x3a1c, 0x7c85, 0xe10d, 0x526c, 0xd512,
    0xad75, 0xe01d, 0x03fb, 0xcc79, 0xe114, 0xe0c9, 0x24ba, 0x0056, 0x61d7, 0xac99,
    0x8d73, 0xe286, 0x6369, 0x030d, 0x9e89, 0xbbea, 0x15b5, 0x31c8, 0x64c1, 0xcbfe,
    0x4020, 0xf616, 0xce9d, 0xa449, 0xc206, 0xaeb5, 0xab73, 0x3013, 0xf39d, 0xf7ce,
    0xe205, 0x7fc7, 0x7884, 0xde6e, 0x351f, 0x784f, 0xd206, 0xcb79, 0xb661, 0x8880,
    0x41c2, 0x3fa0, 0x6388, 0xb826, 0x7d2e, 0x8232, 0x6816, 0x34ee, 0xaf70, 0x9710,
    0x6da1, 0xca9e, 0xc0e5, 0x2879, 0x8f94, 0xff12, 0xa051, 0xc898, 0xc609, 0x13c2,
    0xc0eb, 0x8b67, 0x4320, 0xa842, 0xf8e7, 0x9ddc, 0x7064, 0x57fd, 0xfe3a, 0x51c3,
    0xc22c, 0x8162, 0x4ae7, 0x709f, 0x3f84, 0x6cef, 0x4aaa, 0xcb55, 0xa94a, 0xa5f3,
    0x7b5f, 0x4e19, 0x3171, 0xfc38, 0x7a5d, 0x1e28, 0x6601, 0xb829, 0xf034, 0xc3fb,
    0xc096, 0x465a, 0xacae, 0x5db7, 0x6510, 0xa18a, 0xde05, 0x8409, 0x6ef7, 0x7e61,
    0xed20, 0xebda, 0x17b0, 0x511b, 0xeb70, 0xca92, 0x0470, 0x316a, 0x0000, 0x0000
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
