/*
 * disk/plan9.c
 * 
 * Custom format as used in Plan 9 From Outer Space by Gremlin.
 * 
 * Written in 2023 by Keith Krellwitz.  
 * 
 * RAW TRACK LAYOUT:
 *  11 back-to-back sectors with no gap.
 * RAW SECTOR:
 *  u32 0x44894489 sync
 *  u32 0x44894489 sync and padding
 *  u16 sec
 *  u8 data[550]
 *  u16 checksum
 *
 * Checksum is the sum of decoded words
 *
 * TRKTYP_plan_9 data layout:
 *  u8 sector_data[11][550]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *plan9_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->nr_sectors*(ti->bytes_per_sector));
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u, bitoff;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        uint16_t csum, sum, raw[2], dat[ti->bytes_per_sector/2], sec;
 
        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (s->word != 0x44895555)
            continue;

        bitoff = s->index_offset_bc - 47;

        /* sector */
        if (stream_next_bytes(s, raw, 4) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &sec);
        sec = be16toh(sec);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* read and decode data. */
        for (i = sum = 0; i < ti->bytes_per_sector/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &dat[i]);
            sum += be16toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 4) == -1)
            goto done;
        mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &csum);

        if (be16toh(csum) != sum)
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

static void plan9_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint16_t sum;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895555);
        /* sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, i);

        /* data */
        for (j = sum = 0; j < ti->bytes_per_sector/2; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, 
                be16toh(dat[i*ti->bytes_per_sector/2+j]));
            sum += be16toh(dat[i*ti->bytes_per_sector/2+j]);
        }
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, sum);
    }
}

struct track_handler plan9_handler = {
    .bytes_per_sector = 548+2,
    .nr_sectors = 11,
    .write_raw = plan9_write_raw,
    .read_raw = plan9_read_raw
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
