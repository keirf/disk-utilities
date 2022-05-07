/*
 * disk/robocod.c
 * 
 * Custom format as used by "James Pond 2: Codename Robocod" by Millennium.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,0xff,0xff,trknr
 *  u32 csum
 *  u32 data[11][512/4]
 * MFM encoding of sectors:
 *  AmigaDOS-style per-sector encoding (512 bytes even; 512 bytes odd).
 *  AmigaDOS-style checksum over first 10 sectors only! (Rainbird style!)
 * 
 * TRKTYP_robocod data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *robocod_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*512/4], dat[11][512/4], hdr, csum;
        unsigned int i;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw_dat, 16) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[0], &hdr);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[2], &csum);
        hdr = be32toh(hdr);
        csum = be32toh(csum);

        if (hdr != (0xffffff00u | tracknr))
            continue;

        for (i = 0; i < ti->nr_sectors; i++) {
            if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 512, raw_dat, dat[i]);
        }
        if (amigados_checksum(dat, 10*512) != csum)
            continue;

        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 102200) ? 105500 : 100150;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void robocod_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, 10*512)); /* over 10 sectors only! */

    for (i = 0; i < ti->nr_sectors; i++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 512, &dat[i*512/4]);
}

struct track_handler robocod_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = robocod_write_raw,
    .read_raw = robocod_read_raw
};

/*
 * Custom format as used on Adventure of Robinhood and James Pond III by Millennium.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u32 Track Number :: or'd with the the TRKTYP hdr
 *  u32 Checksum sum over data and if carry add 1
 *  u32 dat[6272/4]
 *
 * TRKTYP_millennium_a data layout:
 *  u8 sector_data[6272]
 * TRKTYP_millennium_b data layout:
 *  u8 sector_data[6272]
 */

struct millennium_info {
    uint16_t type;
    uint32_t hdr;
    unsigned int bitlen;
};

const static struct millennium_info millennium_infos[] = {
    { TRKTYP_millennium_a, 0x00000000, 105500 }, /* Adventures of Robinhood */
    { TRKTYP_millennium_b, 0x00000100, 105500 }  /* James Pond III */
};

static const struct millennium_info *find_millennium_info(uint16_t type)
{
    const struct millennium_info *millennium_info;
    for (millennium_info = millennium_infos; millennium_info->type != type; millennium_info++)
        continue;
    return millennium_info;
}

static void *millennium_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct millennium_info *millennium_info = find_millennium_info(ti->type);

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], trk, csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);
        if (be32toh(trk) != (millennium_info->hdr | tracknr))
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            if(sum + be32toh(dat[i]) < sum)
                sum++;
            sum += be32toh(dat[i]);
        }

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = millennium_info->bitlen;
        return block;
    }

fail:
    return NULL;
}

static void millennium_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct millennium_info *millennium_info = find_millennium_info(ti->type);
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, millennium_info->hdr | tracknr);

    for (i = sum = 0; i < ti->len/4; i++) {
        if(sum + be32toh(dat[i]) < sum)
            sum++;
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler millennium_a_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = millennium_write_raw,
    .read_raw = millennium_read_raw
};
struct track_handler millennium_b_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = millennium_write_raw,
    .read_raw = millennium_read_raw
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
