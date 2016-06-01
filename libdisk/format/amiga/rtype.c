/*
 * disk/rtype.c
 * 
 * Custom format as used by R-Type by Electric Dreams / Factor 5 / Rainbow Arts
 * 
 * Written in 2011 by Keir Fraser
 * 
 * The disk contains four tracks types:
 *   0-  9: AmigaDOS
 *  10- 62: R-Type (variant A)
 *  63- 67  R-Type (variant B)
 *      68: R-Type protection track
 *  69-158: R-Type (variant B)
 *     159: Unused/Unformatted
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* R-Type (variant A): T10-62
 *  u16 0x9521 :: Sync
 *  u8  0      :: bc_mfm
 *  u32 csum   :: bc_mfm_odd, AmigaDOS style checksum
 *  u8  data_even[5968] :: bc_mfm_even
 *  u8  data_odd[5968]  :: bc_mfm_odd
 * TRKTYP_rtype data layout:
 *  u8 sector_data[5968] */

static void *rtype_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint8_t raw_dat[2*ti->len];
        uint32_t csum;
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        csum = s->word & 0x55555555u; /* bc_mfm_odd */

        if (stream_next_bytes(s, raw_dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw_dat, raw_dat);

        if (amigados_checksum(raw_dat, ti->len) != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void rtype_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    csum = amigados_checksum(ti->dat, ti->len);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd, 32, csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, ti->dat);
}

struct track_handler rtype_a_handler = {
    .bytes_per_sector = 5968,
    .nr_sectors = 1,
    .write_raw = rtype_a_write_raw,
    .read_raw = rtype_a_read_raw
};

/* R-Type (variant B): T63-67, T69-158
 *  u16 0x9521 :: Sync
 *  u8  0      :: bc_mfm
 *  u32 data[6552/4] :: bc_mfm_even_odd alternating longs
 *  u32 csum   :: bc_mfm_even_odd, (AmigaDOS-style | 0xaaaaaaaa)
 * TRKTYP_rtype data layout:
 *  u8 sector_data[6552] */

static void *rtype_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t raw_dat[2*ti->len/4];
        uint32_t csum = 0;
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, raw_dat, 2*ti->len) == -1)
            goto fail;
        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[2*i], &raw_dat[i]);
        csum = amigados_checksum(raw_dat, ti->len);
        csum &= 0x55555555u;
        csum |= 0xaaaaaaaau;

        if (stream_next_bytes(s, &raw_dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4,
                         &raw_dat[ti->len/4], &raw_dat[ti->len/4]);
        if (csum != be32toh(raw_dat[ti->len/4]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void rtype_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = 0; i < ti->len/4; i++) 
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 4, &dat[i]);

    csum = amigados_checksum(dat, ti->len);
    csum &= 0x55555555u;
    csum |= 0xaaaaaaaau;
    
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler rtype_b_handler = {
    .bytes_per_sector = 6552,
    .nr_sectors = 1,
    .write_raw = rtype_b_write_raw,
    .read_raw = rtype_b_read_raw
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
