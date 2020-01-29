/*
 * disk/zzkj.c
 * 
 * Custom formats as used on ZZKJ titles.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* TRKTYP_zzkj_boot: Used on Disk 1, Track 2 only.
 *  u16 0x4489,0x4489,0x2aaa
 *  u32 data_odd[0x402]
 *  u32 data_even[0x402]
 *  First data long must be '1'.
 *  Checksum is last data long, ADD.L of all preceding data longs
 *  Track length is normal (not long)
 * TRKTYP_zzkj_boot data layout:
 *  u8 sector_data[4*1024] */

static void *zzkj_boot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, dat[0x402*2];
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
            break;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        if (be32toh(dat[0]) != 1)
            continue;

        for (i = csum = 0; i < 0x401; i++)
            csum += be32toh(dat[i]);
        if (csum != be32toh(dat[0x401]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat[1], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

    return NULL;
}

static void zzkj_boot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[0x402];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    dat[0] = htobe32(1);
    memcpy(&dat[1], ti->dat, ti->len);
    for (i = csum = 0; i < 0x401; i++)
        csum += be32toh(dat[i]);
    dat[0x401] = htobe32(csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, 0x402*4, dat);
}

struct track_handler zzkj_boot_handler = {
    .bytes_per_sector = 4*1024,
    .nr_sectors = 1,
    .write_raw = zzkj_boot_write_raw,
    .read_raw = zzkj_boot_read_raw
};

/* TRKTYP_zzkj_11sec:
 * 11 sectors:
 *  u16 0x4489,0x4489,0x2aaa :: Sync header
 *  u32 data_odd[0x82]
 *  u32 data_even[0x82]
 *  u16 0xaaaa,0xaaaa,0xaaaa :: Sector gap
 *  First data long contains cylinder and sector numbers.
 *  Next 0x80 longs (512 bytes) are sector data.
 *  Last data long is ADD.L checksum of all preceding data longs.
 *  Track length is normal (not long)
 * TRKTYP_zzkj_11sec data layout:
 *  u8 sector_data[11*512] */

static void *zzkj_11sec_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len+1);
    unsigned int nr_valid_blocks = 0;

    ti->data_bitoff = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t csum, dat[0x82*2], idx_off;
        unsigned int i, sec;

        if (s->word != 0x44894489)
            continue;

        idx_off = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, sizeof(dat)/2, dat, dat);

        for (i = csum = 0; i < 0x81; i++)
            csum += be32toh(dat[i]);
        if (csum != be32toh(dat[0x81]))
            continue;

        if ((be32toh(dat[0]) >> 8) != (tracknr>>1))
            continue;

        sec = (uint8_t)be32toh(dat[0]);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        memcpy(&block[sec*512], &dat[1], 512);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;

        /* Sector 0 is not necessarily first written. First written is always
         * first after index mark. So we simply scan for that. */
        if (ti->data_bitoff > idx_off) {
            ti->data_bitoff = idx_off;
            block[ti->len] = sec; /* remember first sector */
        }
    }

    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    ti->len++; /* for space to remember which is the first sector */
    return block;
}

static void zzkj_11sec_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[0x82];
    unsigned int i, j, sec;

    for (i = 0; i < ti->nr_sectors; i++) {
        sec = (i + ti->dat[ti->len-1]) % ti->nr_sectors;

        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

        dat[0] = htobe32(((tracknr>>1)<<8) | sec);
        memcpy(&dat[1], &ti->dat[sec*512], 512);
        for (j = csum = 0; j < 0x81; j++)
            csum += be32toh(dat[j]);
        if (!is_valid_sector(ti, sec))
            csum = ~csum; /* bad checksum for an invalid sector */
        dat[0x81] = htobe32(csum);

        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, 0x82*4, dat);

        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    }
}

struct track_handler zzkj_11sec_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = zzkj_11sec_write_raw,
    .read_raw = zzkj_11sec_read_raw
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
