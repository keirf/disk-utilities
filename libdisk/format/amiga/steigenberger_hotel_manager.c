/*
 * disk/steigenberger_hotel_manager.c
 *
 * Custom format as used Steigenberger HotelManager by Bomico
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4849 Sync
 *  u16 0xaaaa or decoded 0
 *  u32 checksum and sig
 *  u32 dat[ti->len/4]
 * 
 * TRKTYP_steigenberger_hotel_manager data layout:
 *  u8 sector_data[5888]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *steigenberger_hotelmanager_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], hdr;
        uint8_t sum, csum;
        unsigned int i, count;
        char *block;

        if ((uint16_t)s->word != 0x4849)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        // checksum and sig
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        // check for sig
        if ((uint16_t)be32toh(hdr) != 0x4653)
            continue;

        // check track number
        if ((uint8_t)(be32toh(hdr) >> 24) != tracknr)
            continue;

        csum = (uint8_t)(be32toh(hdr) >> 16);

        // data and checksum calculation
        count = 5887;
        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= (uint8_t)(be32toh(dat[i]) >> 24) + count;
            count--;
            sum ^= (uint8_t)(be32toh(dat[i]) >> 16) + count;
            count--;
            sum ^= (uint8_t)(be32toh(dat[i]) >> 8) + count;
            count--;
            sum ^= (uint8_t)be32toh(dat[i]) + count;
            count--;
        }

        if (csum != sum)
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

static void steigenberger_hotelmanager_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, hdr;
    uint8_t sum;
    unsigned int i, count;

    // sync
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4849);

    // padding
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

    // checksum
    count = 5887;
    for (i = sum = 0; i < ti->len/4; i++) {
        sum ^= (uint8_t)(be32toh(dat[i]) >> 24) + count;
        count--;
        sum ^= (uint8_t)(be32toh(dat[i]) >> 16) + count;
        count--;
        sum ^= (uint8_t)(be32toh(dat[i]) >> 8) + count;
        count--;
        sum ^= (uint8_t)be32toh(dat[i]) + count;
        count--;
    }

    hdr = 0x00004653u | tracknr << 24 | sum << 16;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    // data
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler steigenberger_hotelmanager_handler = {
    .bytes_per_sector = 5888,
    .nr_sectors = 1,
    .write_raw = steigenberger_hotelmanager_write_raw,
    .read_raw = steigenberger_hotelmanager_read_raw
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
