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
#include "private.h"

#include <arpa/inet.h>


/*
 * R-Type (variant A): T10-62
 *  u16 0x9521 :: Sync
 *  u8  0      :: MFM_all
 *  u32 csum   :: MFM_odd, AmigaDOS style checksum
 *  u8  data_even[5968] :: MFM_even
 *  u8  data_odd[5968]  :: MFM_odd
 * TRKTYP_rtype data layout:
 *  u8 sector_data[5968]
 */

static void *rtype_a_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    ti->bytes_per_sector = 5968;
    ti->nr_sectors = 1;
    ti->len = ti->bytes_per_sector * ti->nr_sectors;

    while (stream_next_bit(s) != -1) {

        uint8_t raw_dat[2*ti->len];
        uint32_t csum;
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != 0)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        csum = mfm_decode_bits(MFM_odd, s->word);

        if (stream_next_bytes(s, raw_dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, ti->len, raw_dat, raw_dat);

        if (amigados_checksum(raw_dat, ti->len) != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        return block;
    }

fail:
    return NULL;
}

static void rtype_a_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);

    csum = amigados_checksum(ti->dat, ti->len);
    tbuf_bits(tbuf, SPEED_AVG, MFM_odd, 32, csum);

    tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, ti->len, ti->dat);
}


/*
 * R-Type (variant B): T63-67, T69-158
 *  u16 0x9521 :: Sync
 *  u8  0      :: MFM_all
 *  u32 data[6552/4] :: MFM_even_odd alternating longs
 *  u32 csum   :: MFM_even_odd, (AmigaDOS-style | 0xaaaaaaaa)
 * TRKTYP_rtype data layout:
 *  u8 sector_data[6552]
 */

static void *rtype_b_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    ti->bytes_per_sector = 6552;
    ti->nr_sectors = 1;
    ti->len = ti->bytes_per_sector * ti->nr_sectors;

    while (stream_next_bit(s) != -1) {

        unsigned int i;
        uint32_t raw_dat[2*ti->len/4];
        uint32_t csum = 0;
        char *block;

        if ((uint16_t)s->word != 0x9521)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, raw_dat, 2*ti->len) == -1)
            goto fail;
        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(MFM_even_odd, 4, &raw_dat[2*i], &raw_dat[i]);
        csum = amigados_checksum(raw_dat, ti->len);
        csum &= 0x55555555u;
        csum |= 0xaaaaaaaau;

        if (stream_next_bytes(s, &raw_dat[ti->len/4], 8) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, 4,
                         &raw_dat[ti->len/4], &raw_dat[ti->len/4]);
        if (csum != ntohl(raw_dat[ti->len/4]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void rtype_b_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x9521);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);

    for (i = 0; i < ti->len/4; i++) 
        tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, 4, &dat[i]);

    csum = amigados_checksum(dat, ti->len);
    csum &= 0x55555555u;
    csum |= 0xaaaaaaaau;
    
    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, csum);
}


/*
 * R-Type Protection Track: T68
 *  u16 0x4489 :: Sync
 *  u8  0      :: MFM_all
 *  u32 csum   :: MFM_even_odd
 *  u32 data[0xc8] :: 0xc8 longs, XORed together == csum ^ 0x12345678?
 * TRKTYP_rtype data layout:
 *  No data for protection track.
 * This is all pretty sketchy, since the protection 'check' at the game intro
 * screen fails even with a known good disk image. So here we only check for
 * sync plus an encoded nul byte, and that is also all we generate for this
 * track. If we find a real protection check later in the game, we can deal
 * with that here.
 */

static void *rtype_prot_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    ti->bytes_per_sector = 0;
    ti->nr_sectors = 0;
    ti->len = ti->bytes_per_sector * ti->nr_sectors;

    while (stream_next_bit(s) != -1) {
        if ((uint16_t)s->word != 0x4489)
            continue;
        if (stream_next_bits(s, 16) == -1)
            continue;
        if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != 0)
            continue;
        return memalloc(0);
    }

    return NULL;
}

static void rtype_prot_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4489);
}


/*
 * R-Type track dispatcher
 */

static void *rtype_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    if (tracknr == 68)
        return rtype_prot_write_mfm(d, tracknr, s);
    if (tracknr >= 63)
        return rtype_b_write_mfm(d, tracknr, s);
    return rtype_a_write_mfm(d, tracknr, s);
}

static void rtype_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    if (ti->len == 5968)
        return rtype_a_read_mfm(d, tracknr, tbuf);
    if (ti->len == 6552)
        return rtype_b_read_mfm(d, tracknr, tbuf);
    return rtype_prot_read_mfm(d, tracknr, tbuf);
}

struct track_handler rtype_handler = {
    .write_mfm = rtype_write_mfm,
    .read_mfm = rtype_read_mfm
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
