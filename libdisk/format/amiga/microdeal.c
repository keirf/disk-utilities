/*
 * disk/microdeal.c
 *
 * This decoder is used by Time Bandit from Microdeal.
 *  
 * It looks like the track was set up with sectors, but the
 * data appears to have been read in and decoded as a single
 * sector and written back with only the sync at the beginning
 * of the track left intact.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *time_bandit_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    while (stream_next_bit(s) != -1) {

        uint8_t raw[2], dat[ti->len];
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;

        /* sync */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        /* data */
        for (i = 0; i < ti->len-2; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm, 1, raw, &dat[i]);
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = (s->track_len_bc/100)*100+100;
        return block;
    }

fail:
    return NULL;
}

static void time_bandit_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    /* data */
    for (i = 0; i < ti->len-2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
    }
}

struct track_handler time_bandit_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = time_bandit_write_raw,
    .read_raw = time_bandit_read_raw
};


static void *microdeal_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len+40*ti->bytes_per_sector);
    unsigned int i, nr_valid_blocks = 0, counter = 0, 
        least_block = ~0u;
    uint8_t raw[2], dat[ti->bytes_per_sector];

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        uint8_t hdr[40];
        unsigned int bitoff;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x4489)
            continue;

        bitoff = s->index_offset_bc - 47;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint8_t)mfm_decode_word((uint16_t)s->word) != IBM_MARK_IDAM)
            continue;

        for (i = 0; i < 40; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                break;
            mfm_decode_bytes(bc_mfm, 1, raw, &hdr[i]);
        }

        if (stream_next_bits(s, 32) == -1)
            break;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x4489)
            continue;
        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint8_t)mfm_decode_word((uint16_t)s->word) != IBM_MARK_DAM)
            continue;

        /* decode data */
        for (i = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                break;
            mfm_decode_bytes(bc_mfm, 1, raw, &dat[i]);
        }

        memcpy(&block[counter*(ti->bytes_per_sector+40)], &hdr, 40);
        memcpy(&block[counter*(ti->bytes_per_sector+40)+40], &dat, ti->bytes_per_sector);
        set_sector_valid(ti, counter);
        nr_valid_blocks++;

        if (least_block > counter) {
            ti->data_bitoff = bitoff;
            least_block = counter;
        }
        counter++;

    }
    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void microdeal_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t dat[ti->bytes_per_sector+40];
    unsigned int i, sec;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        memcpy(dat, &ti->dat[sec*(ti->bytes_per_sector+40)], ti->bytes_per_sector+40);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, IBM_MARK_IDAM);
        /* header */
        for (i = 0; i < 40; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);

        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, IBM_MARK_DAM);
        /* data */
        for (i = 40; i < ti->bytes_per_sector+40; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);

        for (i = 0; i < 8; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    }
}

struct track_handler microdeal_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_raw = microdeal_write_raw,
    .read_raw = microdeal_read_raw
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
