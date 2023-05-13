/*
 * disk/cardiaxx.c
 *
 * Custom format as used on Cardiaxx by Electronic Zoo/Taam 17.
 *
 * Updated in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  6 sectors back to back 
 * 
 *  u16 0x448a 0x448a :: Sync
 *  u16 0 :: padding
 *  u16 dat[1024/2] :: encoded as even/odd block
 *  u16 csum :: encoded as even/odd 
 *  u16 0 :: gap - inconsitent values, but never checked
 *
 * TRKTYP_cardiaxx data layout:
 *  u8 sector_data[6][1024]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *cardiaxx_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int nr_valid_blocks = 0, sec = 0;

    char *block = memalloc(ti->nr_sectors*ti->bytes_per_sector);
    ti->data_bitoff = s->index_offset_bc - 31;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint16_t raw[2], csum, sum;
        uint16_t dat[ti->bytes_per_sector/2];
        unsigned int i;

        /* sync */
        if (s->word != 0x448a448a)
            continue;

        /* padding - never checked */
        if (stream_next_bits(s, 16) == -1)
            break;
        if(mfm_decode_word((uint16_t)s->word) != 0)
            continue;

        /* data */
        for (i = sum = 0; i < ti->bytes_per_sector/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto done;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum += be16toh(dat[i]);
        }

        /* Read data checksum. */
        if (stream_next_bytes(s, raw, 4) == -1)
            goto done;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);
        if (be16toh(csum) != 0xffff-sum)
            continue;

        /* Gap - values not consistent */
        if (stream_next_bits(s, 16) == -1)
            break;

        memcpy(&block[sec*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        sec++;
    }

    if (nr_valid_blocks == 0)
        goto done;
    ti->data_bitoff = 0;
    ti->total_bits = 100400;
    return block;

done:
    memfree(block);
    return NULL;

}

static void cardiaxx_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    for (i = 0; i < ti->nr_sectors; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x448a448a);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        dat = (uint16_t *)&ti->dat[ti->bytes_per_sector*i];
        for (j = csum = 0; j < ti->bytes_per_sector/2; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[j]));
            csum += be16toh(dat[j]);
        }
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, 0xffff-csum);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    }
}

struct track_handler cardiaxx_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 6,
    .write_raw = cardiaxx_write_raw,
    .read_raw = cardiaxx_read_raw
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
