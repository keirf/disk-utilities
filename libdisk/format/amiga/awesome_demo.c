/*
 * disk/awesome_demo.c
 *
 * Custom format as used on Awesome Demo by Psygnosis.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 signature - mfm decoded value 0x41575331
 *  u32 dat[ti->len/4]
 * 
 * No checksum found.  Added custom cr check and checked against the
 * official IPF and with the dump from BarryB and they were identical.
 * 
 * Tracks have variable lengths but the max size is 5400 bytes
 * 
 * TRKTYP_awesome_demo data layout:
 *  u8 sector_data[5400]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint32_t crcs[];

static void *awesome_demo_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sig, sum;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* signature*/
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);

        if (be32toh(sig) != 0x41575331)
            continue;

        /* data */
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* custom crc check */
        if (sum != crcs[tracknr-1])
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void awesome_demo_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    /* signature */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0x41575331);

    /* data */
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler awesome_demo_handler = {
    .bytes_per_sector = 5400,
    .nr_sectors = 1,
    .write_raw = awesome_demo_write_raw,
    .read_raw = awesome_demo_read_raw
};

static const uint32_t crcs[] = {
    0x679d71e3, 0x8f5dd486, 0xee10524b, 0x196f7b99, 0x1bbc5a5c, 0x03dd0134,
    0x950dfe0d, 0x15b60738, 0x241c8901, 0xc1e70516, 0x45505474, 0x19e2da23,
    0x81f3a55e, 0x62dc7b17, 0x7432d35d, 0x724a5214, 0x0a32942e, 0xa7018f2d,
    0x48b948b5, 0xa1dacba2, 0xafa30270, 0x38dcac10, 0xe880634c, 0xa0ea9855,  
    0xf90095ca, 0xe458c853, 0xd76354ca, 0x8305decf, 0x14b4a626, 0x1fec9720,  
    0x8a9eb129, 0x20c33059, 0xdbc7b909, 0xb5e6306e, 0x59560d0a, 0x6c1312e0, 
    0xae84acac, 0xa076bebc, 0x341653f9, 0x78caaa19, 0x42e499bc, 0xb7dd1861, 
    0x985a8af9, 0x626cb72f, 0xa23d7c91, 0x5f070c80, 0xff1f398e, 0xfc36a6aa, 
    0xe0267199, 0x61c4cb3b, 0x3c28a425, 0x3effcb38, 0xaa30a290, 0x11d0cf7a, 
    0xd505d9e7, 0xcac79e82, 0xa4f60b73, 0xc518e8e3, 0xd0a48235, 0xdc22d799, 
    0x12ff2320, 0x6a3ad67a, 0x76162649, 0xd06af244, 0x7523236a, 0xdae51445, 
    0x76aa8634, 0xbd030a5f, 0x202ae4f9, 0xcbfa42f6, 0x43e70333, 0x88295e81, 
    0x36e147cc, 0x26852037, 0x41dbbd5d, 0xe171d403, 0x3c4663e3, 0x819d2f69, 
    0x05340fd8, 0x371a5459, 0x42ff94db, 0x2b92d4c4, 0x295ccb41, 0x18d81f22, 
    0x7de4553a, 0xcb2ec93f, 0xf41f9750, 0xa970fa69, 0x4d0fce46, 0xe653b28d, 
    0x5bf1aea4, 0x3bb63b51, 0x66e63c2d, 0x2fd812ed, 0x87a4277c, 0x7a1902aa, 
    0xdc7ceda0, 0x7f403487, 0x23f14aa4, 0x08f2d62d, 0xe4c89e5e, 0xeff0e852, 
    0x7d66c91a, 0xf2b5ce9d, 0x6ac5e0e7, 0xb50714cd, 0x851b6ea8, 0x74c7a39c, 
    0xe5cb4c01, 0x50e83dd9, 0x248e76b1, 0x05f98940, 0xf966ae5b, 0xdb6bfa62, 
    0x2777d6c6, 0x02a80cb5, 0xdc804a15, 0x0546627a, 0xfa660f3a, 0xba51598f, 
    0xd2d68874, 0xe8845b91, 0x9186e623, 0xf4d4b126, 0x2af10fa3, 0xe92b70ca, 
    0xc7397f2e, 0x5ee95559, 0xa52f15a2, 0xe10ef9c8, 0x6f17e495, 0x4bd1043d, 
    0x2f37d99e, 0x670d5a8d, 0x4a00af07, 0xd8e8757f, 0x3a1a0eca, 0xb7e2dc23, 
    0x429a8b6f, 0xbbb627ad, 0x358bcb83, 0xe7984add, 0xc14fb31b, 0x1ed9b261, 
    0xaa4f78ea, 0xe0d3590a, 0xc7efa6db, 0x46aa2751, 0x1591cf80, 0x15daccb9, 
    0x0602abdf, 0x7ba7d673, 0x865c91a1, 0xbf62b36e, 0x8e366fcf, 0x044389ac, 
    0x9c017afb, 0xc24008e5, 0xa7677088 
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
