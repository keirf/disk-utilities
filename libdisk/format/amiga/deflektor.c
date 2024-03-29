/*
 * disk/deflektor.c
 * 
 * Custom format as used in Deflektor by Gremlin.
 * 
 * Written in 2022 by Keith Krellwitz.  This is based on
 * Keir Fraser's Federation Of Free Traders decoder.
 * 
 * RAW TRACK LAYOUT:
 *  3 back-to-back sectors with explicit sector gap.
 *  Total encoded sector size, including gap, is 0xfc8 (4040) bytes.
 * RAW SECTOR:
 *  u32 0x44894489 sync marks
 *  u8 0xff
 *  u8 trk^1
 *  u8 sec
 *  u8 checksum upper byte
 *  u8 checksum lower byte
 *  u8 data[2000]
 *  u8 gap[13]
 * 
 * MFM encoding:
 *  No even/odd split
 * 
 * Checksum is the sum of decoded words
 * 
 * TRKTYP_deflektor data layout:
 *  u8 sector_data[3][2000]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *deflektor_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->len);
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t idx_off = s->index_offset_bc - 31;
        uint16_t csum, sum;
        uint8_t sec, *p;

        if (s->word != 0x44894489)
            continue;

        /* track number */
        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (mfm_decode_word(s->word) != (0xff00 | (tracknr^1)))
            continue;

        /* sector */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        sec = mfm_decode_word((uint16_t)s->word);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* checksum */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        csum = mfm_decode_word((uint16_t)s->word);

        if (stream_next_bits(s, 16) == -1)
            goto done;
        csum = (csum << 8 | mfm_decode_word((uint16_t)s->word));

        /* data */
        p = &block[sec * ti->bytes_per_sector];
        for (i = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto done;
            p[i] = mfm_decode_word((uint16_t)s->word);
        }

        /* data checksum calculation */
        for (i = sum = 0; i < ti->bytes_per_sector; i+=2)
            sum += ((uint16_t)p[i] << 8 | p[i+1]);

        if (csum != sum)
            continue;

        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        if (least_block > sec) {
            ti->data_bitoff = idx_off;
            least_block = sec;
        }
    }

done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    for (i = 0; i < ti->nr_sectors; i++)
        if (is_valid_sector(ti, i))
            break;
    ti->data_bitoff -= i * 0xfc8;

    return block;
}

static void deflektor_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint16_t sum;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {

        /* header */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr^1);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, i);

        /* csum */
        for (j = sum = 0; j < ti->bytes_per_sector; j+=2)
            sum += ((uint16_t)dat[j] << 8 | dat[j+1]);

        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sum >> 8);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)sum);

        /* data */
        for (j = 0; j < ti->bytes_per_sector; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[j]);
        }

        /* gap */
        for (j = 0; j < 13; j++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        dat += ti->bytes_per_sector;
    }
}

struct track_handler deflektor_handler = {
    .bytes_per_sector = 2000,
    .nr_sectors = 3,
    .write_raw = deflektor_write_raw,
    .read_raw = deflektor_read_raw
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
