/*
 * disk/super_hang_on.c
 *
 * Custom format as used on Super Hang-On by Data East.
 * 
 * 'v2' format alternative is to match the naming used in the WHDLoad slave
 * 
 * Written in 2014 by Keith Krellwitz
 * version 2 support added in 2021 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489
 *  u32 0x2aaaaaaa
 *  u32 0xaaaaaaaa (weak in v2 format)
 *  u32 0xaaaaaaaa (weak in v2 format)
 *  u32 0xaaaaaaaa
 *  u32 0x44894489
 *  u16 0x2aaa
 *  u32 cylinder             :: Odd
 *  u8  dat[0x1600]          :: Odd
 *  u32 csum                 :: Odd
 *  u32 cylinder             :: Even
 *  u8  dat[0x1600]          :: Even
 *  u32 csum                 :: Even
 *
 * TRKTYP_super_hang_on data layout:
 *  u8 sector_data[5632]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *super_hang_on_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, sum, dat[(0x1600/4+2)*2];
        unsigned int i;
        char *block;
        int v2 = 0;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < 4; i++) {
            uint32_t x = i ? 0xaaaaaaaa : 0x2aaaaaaa;
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != x) {
                /* v2 has weak bits in the header */
                v2 = 1;
                break;
            }
        }

        for (i = 0; i < 32*10; i++) {
            if (s->word == 0x44894489)
                break;
            if (stream_next_bit(s) == -1)
                goto fail;
        }

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        if (be32toh(dat[0]) != tracknr/2)
            continue;

        /* One side of v2 is XOR-encrypted. */
        if (v2 && !(tracknr&1)) {
            uint32_t key = 0x12345678;
            for (i = 1; i <= 0x581; i++) {
                key ^= be32toh(dat[i]);
                dat[i] = htobe32(key);
            }
        }

        for (i = sum = 0; i <= 0x580; i++)
            sum += be32toh(dat[i]);
        csum = be32toh(dat[i]);

        if (csum != sum)
            continue;

        init_track_info(ti, v2 ? TRKTYP_super_hang_on_v2
                        : TRKTYP_super_hang_on);
        block = memalloc(ti->len);
        memcpy(block, &dat[1], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void super_hang_on_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    int v2 = ti->type == TRKTYP_super_hang_on_v2;
    uint32_t sum, dat[0x1600/4 + 2];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    if (v2) {
        tbuf_weak(tbuf, 32);
    } else {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    dat[0] = htobe32(tracknr/2);
    memcpy(&dat[1], ti->dat, ti->len);

    for (i = sum = 0; i <= 0x580; i++)
        sum += be32toh(dat[i]);
    dat[i] = htobe32(sum);

    if (v2 && !(tracknr&1)) {
        uint32_t nkey, key = 0x12345678;
        for (i = 1; i <= 0x581; i++) {
            nkey = be32toh(dat[i]);
            dat[i] ^= htobe32(key);
            key = nkey;
        }
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, sizeof(dat), dat);
}

struct track_handler super_hang_on_handler = {
    .bytes_per_sector = 0x1600,
    .nr_sectors = 1,
    .write_raw = super_hang_on_write_raw,
    .read_raw = super_hang_on_read_raw
};

struct track_handler super_hang_on_v2_handler = {
    .bytes_per_sector = 0x1600,
    .nr_sectors = 1,
    .write_raw = super_hang_on_write_raw,
    .read_raw = super_hang_on_read_raw
};

/*
 * Custom format as used on Super Hang-On by Data East.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489
 *  u16 0x2aaa
 *  u32 0                   :: Odd
 *  u8  dat[0x800]          :: Odd
 *  u32 csum                :: Odd
 *  u32 0                   :: Even
 *  u8  dat[0x800]          :: Even
 *  u32 csum                :: Even
 *
 * TRKTYP_super_hang_on_scores data layout:
 *  u8 sector_data[2048]
 */

static void *super_hang_on_scores_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, sum;
        uint16_t dat[ti->len+8];
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            continue;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        if (be32toh(dat[0]) != 0)
            continue;

        for (i = sum = 0; i < 0x402; i+=2)
            sum += (be16toh(dat[i])<<16) | be16toh(dat[i+1]);
        csum = (be16toh(dat[0x402])<<16) | be16toh(dat[0x403]);

        if (csum != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

    return NULL;
}

static void super_hang_on_scores_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum;
    uint16_t dat[0x404];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    dat[0] = 0;
    dat[1] = htobe16(tracknr/2);
    memcpy(&dat[2], ti->dat, ti->len);

    for (i = csum = 0; i < 0x402; i+=2)
        csum += (be16toh(dat[i])<<16) | be16toh(dat[i+1]);
    dat[0x402] = htobe16(csum>>16);
    dat[0x403] = htobe16(csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, 0x404*2, dat);
}

struct track_handler super_hang_on_scores_handler = {
    .bytes_per_sector = 2048,
    .nr_sectors = 1,
    .write_raw = super_hang_on_scores_write_raw,
    .read_raw = super_hang_on_scores_read_raw
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
