/*
 * disk/archipelagos.c
 * 
 * Custom format as used in Archipelagos by Logotron Entertainment.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  5 back-to-back sectors with explicit sector gap.
 *  Total encoded sector size, including gap, is 0x820 (2080) bytes.
 * RAW SECTOR:
 *  u8 0xa1,0xa1   :: 0x4489 sync marks
 *  u8 0xff
 *  u8 trk,sec+1
 *  u16 csum
 *  u8 data[1024]
 *  u8 gap[9]
 * MFM encoding:
 *  No even/odd split
 * 
 * TRKTYP_archipelagos data layout:
 *  u8 sector_data[5][1024]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *archipelagos_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t idx_off = s->index_offset_bc - 31;
        uint16_t csum, w, *p;
        uint8_t sec;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto done;
        if (mfm_decode_word(s->word) != (0xff00 | tracknr))
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto done;
        sec = mfm_decode_word((uint16_t)s->word) - 1;
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto done;
        csum = mfm_decode_word(s->word);

        p = (uint16_t *)(block + sec * ti->bytes_per_sector);
        for (i = 0; i < ti->bytes_per_sector/2; i++) {
            if (stream_next_bits(s, 32) == -1)
                goto done;
            csum -= w = mfm_decode_word(s->word);
            *p++ = htobe16(w);
        }

        if (csum)
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
    ti->data_bitoff -= i * 0x820;

    /* Some releases use long tracks (for no good reason). */
    stream_next_index(s);
    ti->total_bits = (s->track_len_bc > 102000) ? 105500 : 100150;

    return block;
}

static void archipelagos_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        uint16_t csum = 0;
        /* header */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, i+1);
        /* csum */
        for (j = 0; j < ti->bytes_per_sector/2; j++)
            csum += be16toh(dat[j]);
        if (!is_valid_sector(ti, i))
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, csum);
        /* data */
        for (j = 0; j < 512; j++, dat++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, be16toh(*dat));
        /* gap */
        for (j = 0; j < 9; j++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    }
}

struct track_handler archipelagos_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 5,
    .write_raw = archipelagos_write_raw,
    .read_raw = archipelagos_read_raw
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
