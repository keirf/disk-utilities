/*
 * disk/street_gang.c
 *
 * Custom format as used on Street Gang by Players
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u8  0
 *  u32 header
 *      uint8_t track number 
 *      uint8_t checksum
 *      0x544c  signature
 *  u32 dat[ti->len/4]
 * 
 * The checksum is the calculate by
 *   count = data length - 1
 *   iterate over decoded bytes of data
 *     sum += count
 *     count--
 *     sum ^= single byte of decoded data 
 *
 * TRKTYP_street_gang data layout:
 *  u8 sector_data[5888]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint8_t checksum(uint32_t *dat, uint32_t data_length)
{
    unsigned int i;
    uint16_t count;
    uint8_t sum;

    count = data_length - 1;
    for (i = sum = 0; i < data_length/4; i++) {
        sum ^= (uint8_t)((be32toh(dat[i]) >> 24) + (uint8_t)count);
        count--;
        sum ^= (uint8_t)((be32toh(dat[i]) >> 16) + (uint8_t)count);
        count--;
        sum ^= (uint8_t)((be32toh(dat[i]) >> 8) + (uint8_t)count);
        count--;
        sum ^= (uint8_t)(be32toh(dat[i]) + (uint8_t)count);
        count--;
    }
    return sum;
}

static void *street_gang_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], hdr;
        unsigned int i;
        uint8_t trk, csum, sum;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        if ((uint16_t)be32toh(hdr) != 0x544c)
            continue;

        csum = (uint8_t)(be32toh(hdr) >> 16);
        trk = (uint8_t)(be32toh(hdr) >> 24);

        if (tracknr != trk)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        sum = checksum(dat,ti->len);
        if (csum != sum)
           continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100800;
        return block;
    }

fail:
    return NULL;
}

static void street_gang_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, hdr;
    uint8_t sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    sum = checksum(dat,ti->len);
    hdr = (tracknr << 24) | (sum << 16) | 0x544c;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler street_gang_handler = {
    .bytes_per_sector = 5888,
    .nr_sectors = 1,
    .write_raw = street_gang_write_raw,
    .read_raw = street_gang_read_raw
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
