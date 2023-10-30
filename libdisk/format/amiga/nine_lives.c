/*
 * disk/nine_lives.c
 *
 * Custom format as used in nine_lives
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489
 *  u32 Track Number/2 in the first uint8_t (rest of the bytes 
 *      appear to be random data)
 *  u32 data[ti->len/4]
 *
 * There is not checksum. Created checksums based on the official
 * IPF.
 * 
 * TRKTYP_nine_lives_a data layout:
 *  u8 sector_data[5120]
 * 
 * TRKTYP_nine_lives_b data layout:
 *  u8 sector_data[6144]
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint32_t crcs[];

static void *nine_lives_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[(ti->len/4)*2], raw[2], trk, sum;
        char *block;
        unsigned int i;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, sizeof(dat)/2, dat, dat);

        dat[ti->len/4] = 0xffffff00 & be32toh(trk);

        for (i = sum = 0; i < ti->len/4; i++)
            sum ^= be32toh(dat[i]);

        if (sum != crcs[tracknr])
            goto fail;

        block = memalloc(ti->len+4);
        ti->total_bits = 105600;
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void nine_lives_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, dat[ti->len/4] | (uint8_t)(tracknr/2));

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler nine_lives_a_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = nine_lives_write_raw,
    .read_raw = nine_lives_read_raw
};

struct track_handler nine_lives_b_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = nine_lives_write_raw,
    .read_raw = nine_lives_read_raw
};

static const uint32_t crcs[] = {
    0x00000000, 0x35d34b6d, 0x045bb079, 0xfa5e4571, 
    0x5c0b4dce, 0xa8148fed, 0x0241bcb2, 0x76070ae6, 
    0xf6c28fe3, 0x29496a0b, 0xc59c53e9, 0xb6cf4d41, 
    0x00000000, 0xbf71ced7, 0x08b843e0, 0x0c4d5be2, 
    0x68a8a3fa, 0x05754454, 0x58e0ca80, 0xbf769d9f, 
    0x45ee4f28, 0xf6971a6f, 0x36dca2d2, 0x68861ec4, 
    0x0e5a7ec2, 0x9e4f7460, 0x43aaa812, 0x51fe1d8e, 
    0xec938f6d, 0xd79679b8, 0x47355636, 0xad9f257e, 
    0x1b409b42, 0x8081a018, 0x24a2876a, 0x175deafe, 
    0x7a84813a, 0x0e682f5c, 0x52b92f72, 0x74423283, 
    0x9a1b521b, 0x79d27ca6, 0xfac1f8fe, 0xf28ed50f, 
    0x4836908e, 0x221140b7, 0xe927e5a6, 0x9953ff6d, 
    0x1f07d2bc, 0x1d9f7485, 0xffa474f0, 0xf8b4a9a0, 
    0xd437b0ec, 0x1cabad13, 0x75da3596, 0xac510c34, 
    0x2a7ef318, 0xe8373f26, 0x0e138605, 0xfe887fd2, 
    0x6a70e637, 0x0e0d5cf9, 0x5d1d4d46, 0x891ad8d5, 
    0x0d4aed30, 0x2043f904, 0xe209343c, 0xff48d42e, 
    0xd1057a14, 0x749927fe, 0xc2818608, 0xe250ecfb, 
    0xb4051bc9, 0x28dd2eb7, 0x063a46ff, 0x0f255b59, 
    0xa1071643, 0x0428847a, 0x3e9c8c03, 0x2bca738b, 
    0xd21948e3, 0xb0a93ed4, 0xc0aa5222, 0x4cb6b75e, 
    0xe02638f1, 0xb3f2cc54, 0xd5b5356d, 0xc089abdf, 
    0x339d1631, 0x00000000, 0x845cc8da, 0x00000000, 
    0x994b4a9d, 0x71a9f682, 0x10bf41c3, 0x4a2fd9ef, 
    0xf7e601f7, 0x59709fb8, 0x38d54f14, 0xf8526a49, 
    0xfbdcf3a8, 0xc797124f, 0x3b1cc2f2, 0x3e957ae3, 
    0xfabbc095, 0xa5e90ee8, 0x50d49e67, 0xfac2d685, 
    0x5fcd9848, 0x090e0054, 0x991cc0e5, 0x03b0f32d, 
    0x2b689823, 0x9e98d8a0, 0xa298f925, 0xc20ce8b0, 
    0xadfd5522, 0x9e9966da, 0x1d924271, 0x6f0581f4, 
    0x67c3b107, 0x666e404e, 0xe8778308, 0x5b0d6f8e, 
    0x74ddca13, 0x85682d2a, 0xddd012b2, 0xb8adce09, 
    0x10ac2439, 0x8c68a567, 0x3393db79, 0x5813eb06, 
    0x2b76efe4, 0x0be8906e, 0x61dbf0cd, 0x4e87b342, 
    0xc9c23599, 0xe94b4cf4, 0x8f3aad93, 0xd1c5f8ff, 
    0x3320770f, 0x0d79dd89, 0x37fee966, 0xfcd3554b, 
    0xb8065a9c, 0x64ed70a5, 0xb507f124, 0x3250f533, 
    0xd99953ae, 0xa4cb5702, 0x3475b0c7, 0x60e7eca9, 
    0x00000000, 0xa99dd203, 0x00000000, 0x3433097f, 
    0x00000000, 0xd414f753, 0x00000000, 0xb37fe938
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
