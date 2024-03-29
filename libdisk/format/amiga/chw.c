/*
 * disk/chw.c
 *
 * Custom format as used on the following games
 * 
 * Ringside by EAS 
 * Leonardo by Starbyte
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u32 track length
 *  u32 header 0x######00 | track number
 *  u32 dat[ti->len/4]
 *  u32 checksum
 *
 * The checksum is calculated eor over the raw data including the track length 
 * and header
 * 
 * Note: Loanardo uses 10+ different track lengths
 * 
 * Header:
 *   Ringside 0x0fa201##
 *   Leonardo US 0x4c5101##
 *   Leonardo EU 0x4c5001##
 * 
 * 
 * TRKTYP_chw data layout:
 *  u8 sector_data[dynamic]
 * 
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *chw_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], csum, sum, trk_len, hdr;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        sum = 0;
        /* track length */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &trk_len);
        trk_len = be32toh(trk_len);

        if (trk_len > 0x1a00 || trk_len < 0x1700)
            continue;

        sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);

        /* header with track number */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &hdr);
        hdr = be32toh(hdr);
        sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);

        if ((uint8_t)hdr != tracknr && tracknr != 144)
            continue;

        /* data */
        uint32_t dat[trk_len/4+2];
        for (i = 2; i < trk_len/4+2; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);
        sum &= 0x55555555;

        if (be32toh(csum) != sum)
            goto fail;

        /* need to pass the track length and header without the track */
        dat[0] = trk_len;
        dat[1] = hdr & 0xffffff00;

        stream_next_index(s);
        block = memalloc(trk_len+8);
        memcpy(block, dat, trk_len+8);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void chw_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum, raw[2], hdr, trk_len;
    unsigned int i;

    sum = 0;
    hdr = dat[1] | tracknr;
    trk_len = dat[0];

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* track length */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, trk_len);

    /* header */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, hdr);

    /* start checksum calulation */
    raw[1] = htobe32(0x44894489); /* get 1st clock bit right for checksum */
    mfm_encode_bytes(bc_mfm_even_odd, 4, &trk_len, raw, be32toh(raw[1]));
    sum ^= raw[0] ^ raw[1];
    mfm_encode_bytes(bc_mfm_even_odd, 4, &hdr, raw, be32toh(raw[1]));
    sum ^= raw[0] ^ raw[1];

    /* data and checksum calulation*/
    for (i = 2; i < trk_len/4+2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[i], raw, be32toh(raw[1]));
        sum ^= be32toh(raw[0]) ^ be32toh(raw[1]);
    }
    sum &= 0x55555555;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, sum);
}

struct track_handler chw_handler = {
    .nr_sectors = 1,
    .write_raw = chw_write_raw,
    .read_raw = chw_read_raw
};


/*
 *
 * Custom format as used on by the following games:
 * 
 *  Apidya
 *  Turrican III
 *  Mr. Nutz
 *
 * Written in 2022 by Keith Krellwitz 
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYP_chw_2a
 *  u16 (4489 Sync)
 *  u16 padding - disk identifier
 *  u32 dat[6144/4]
 *  u32 Checksum
 * 
 * TRKTYP_chw_2b
 *  u16 (4489 Sync)
 *  u16 padding - disk identifier
 *  u16 Track Number / 2
 *  u32 dat[6656/4]
 *  u32 Checksum
 * 
 * 
 * Checksum: The sum of decoded data
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *chw_2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1], sum, csum;
        uint16_t raw16[2], trk;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* disk identifier */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        dat[ti->len/4] = (uint16_t)s->word;
    
        /* track number/2 only for track type TRKTYP_apidya_b */
        if (ti->type == TRKTYP_chw_2b) {
            if (stream_next_bytes(s, raw16, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw16, &trk);
            if (be16toh(trk) != tracknr/2)
                continue;
        }

        /* data */
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            continue;

        stream_next_index(s);
        ti->total_bits = (s->track_len_bc/100)*100+100;
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void chw_2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, (uint16_t)dat[ti->len/4]);

    if (ti->type == TRKTYP_chw_2b) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, tracknr/2);    
    }

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler chw_2a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = chw_2_write_raw,
    .read_raw = chw_2_read_raw
};

struct track_handler chw_2b_handler = {
    .bytes_per_sector = 6656,
    .nr_sectors = 1,
    .write_raw = chw_2_write_raw,
    .read_raw = chw_2_read_raw
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
