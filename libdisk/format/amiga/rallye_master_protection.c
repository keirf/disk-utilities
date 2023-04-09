/*
 * disk/rallye_master_protection.c
 *
 * Custom protection format as used on Rallye Master by EAS
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x84948494 Sync
 *  u16 dat[ti->len/2]
 *
 * TRKTYP_rallye_master_protection data layout:
 *  u8 sector_data[34]
 * 
 * The protection tracks 80.0 and 80.1 are both checked 3 times.
 * The protection compares 17 words to date that is hard coded 
 * into the game code. The raw dump I have was always coming 
 * reading 0x128A after the sync and it should have been 0x12BA, 
 * but the rest of the raw data was identical. If the first 
 * word is either of the 2 values then set the word one to 
 * 0x12BA. A checksum of raw data is checked.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rallye_master_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[ti->len/2], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x84948494)
            continue;
        ti->data_bitoff = s->index_offset_bc - 32;

        for (i = sum = 0; i < ti->len/2; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            dat[i] = s->word;
            sum += dat[i];
        }

        /* The first value should be 0x12BA but was seeing 0x128A
           so replace the first value with the correct value */
        if (dat[0] != 0x12BA && dat[0] != 0x128A)
            continue;
        dat[0] = 0x12BA;

        /* Check checksum with both values 0x128A and 0x12BA 
           in position 0 of the array */
        if (sum != 0x8017 && sum != 0x8047)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 97500;
        return block;
    }

fail:
    return NULL;
}

static void rallye_master_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x84948494);

    for (i = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[i]);
    }
}

struct track_handler rallye_master_protection_handler = {
    .bytes_per_sector = 34,
    .nr_sectors = 1,
    .write_raw = rallye_master_protection_write_raw,
    .read_raw = rallye_master_protection_read_raw
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
