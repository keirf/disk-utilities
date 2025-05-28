/*
 * disk/mentasm.c
 * 
 * Custom format as used in Buggy Balls by Mentasm.
 * 
 * Written in 2025 by Keith Krellwitz.  
 * 
 * RAW TRACK LAYOUT:
 *  12 back-to-back sectors one u16 gap.
 * RAW SECTOR:
 *  u32 0x44894489 sync
 *  u16 0x2aaa padding
 *  u32 header (## 0x68+sec, ## sec, #### checksum )
 *  u32 data[512]
 *  u16 0x2aaa gap
 *
 * Checksum is the sum of decoded words
 *
 * TRKTYP_mentasm data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *mentasm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->nr_sectors*(ti->bytes_per_sector));
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u, bitoff;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        uint16_t csum, sum;
        uint32_t raw[2], dat[ti->bytes_per_sector/2], hdr;
        unsigned int sec;
 
        /* sync */
        if (s->word != 0x44894489)
            continue;

        bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* sector and checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        hdr = be32toh(hdr);
        csum = (uint16_t)hdr;
        sec = (uint8_t)(hdr >> 16);

        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* read, decode data and calculate checksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto done;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            if (i == 0)
                sum = (0x7fff & (uint16_t)(be32toh(raw[0]) >> 16)) ^ sum;
            else
                sum = ((uint16_t)(be32toh(raw[0]) >> 16)) ^ sum;
            sum = ((uint16_t)(be32toh(raw[0]))) ^ sum;
            sum = ((uint16_t)(be32toh(raw[1]) >> 16)) ^ sum;
            sum = ((uint16_t)(be32toh(raw[1]))) ^ sum;
        }

        if (csum != sum)
            continue;

        set_sector_valid(ti, sec);
        memcpy(&block[sec*(ti->bytes_per_sector)], &dat, (ti->bytes_per_sector)); 
        nr_valid_blocks++;

        if (least_block > sec) {
            ti->data_bitoff = bitoff;
            least_block = sec;
        }
    }

done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    stream_next_index(s);
    ti->total_bits = (s->track_len_bc/100)*100+100;
    return block;
}

static void mentasm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector/4], hdr, raw[2];
    uint16_t sum;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        memcpy(dat, &ti->dat[i*(ti->bytes_per_sector)], ti->bytes_per_sector);
        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        /*padding */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);

        /* checksum */
        raw[1] = htobe32(0x44892aaa);
        for (j = sum = 0; j < ti->bytes_per_sector/4; j++) {
            mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[j], raw, be32toh(raw[1]));
            if (j == 0)
                sum = (0x7fff & (uint16_t)(be32toh(raw[0]) >> 16)) ^ sum;
            else
                sum = ((uint16_t)(be32toh(raw[0]) >> 16)) ^ sum;
            sum = ((uint16_t)(be32toh(raw[0]))) ^ sum;
            sum = ((uint16_t)(be32toh(raw[1]) >> 16)) ^ sum;
            sum = ((uint16_t)(be32toh(raw[1]))) ^ sum;
        }

        /* header */
        hdr = ((0x68+i) << 24) | (i << 16) | sum;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
        
        /* data */
        for (j = 0; j < ti->bytes_per_sector/4; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 
                be32toh(dat[j]));
        }

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);
    }
}

struct track_handler mentasm_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = mentasm_write_raw,
    .read_raw = mentasm_read_raw
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
