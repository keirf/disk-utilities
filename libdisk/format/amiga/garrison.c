/*
 * disk/garrison.c
 *
 * Custom format as used on Garrison by Rainbow Arts
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYP_garrison_dir
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 dat[240/4]
 *
 * TRKTYP_garrison_data
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 checksum
 *  u32 dat[4400/4]
 * 
 * Checksum is eor'd decoded data over the fisrt 1025 u32.
 * The games decoder decodes 1100 u32 but only the first 1025 u32s
 * are used to calculate the checksum.
 * 
 * There is no checksum for the directory track
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *garrison_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (ti->type == TRKTYP_garrison_data) {
            for (i = sum = 0; i < 1025; i++) {
                sum ^= be32toh(dat[i]);
            }

            if (be32toh(csum) != sum)
                    continue;
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void garrison_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    if (ti->type == TRKTYP_garrison_data) {
        for (i = sum = 0; i < 1025; i++) {
            sum ^= be32toh(dat[i]);
        }
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    }

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler garrison_dir_handler = {
    .bytes_per_sector = 240,
    .nr_sectors = 1,
    .write_raw = garrison_write_raw,
    .read_raw = garrison_read_raw
};

struct track_handler garrison_data_handler = {
    .bytes_per_sector = 4400,
    .nr_sectors = 1,
    .write_raw = garrison_write_raw,
    .read_raw = garrison_read_raw
};

/*
 * The protection code checks for the longs 0x448954aa 
 * 0x54aa54aa 0x54aa4489 at the beginning of the track
 * 
 * After the initial checks for these values and the check 
 * against the empty track, one more check for the value 
 * 0x55554489 proceeding the initial 3 longs is done.  The 
 * code for this check is in a completely different section 
 * of the game code.
 * 
 */

static void *garrison_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x54aa54aa)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x54aa54aa)
            continue;

        ti->total_bits = (ti->type == TRKTYP_garrison_ii_protection) ? 101800 : 103500;
        return memalloc(0);
    }

fail:
    return NULL;
}

static void garrison_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (i = 0; i < 2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x54aa54aa);
    
    for (i = 0; i < 5; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895555);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
}

struct track_handler garrison_protection_handler = {
    .write_raw = garrison_protection_write_raw,
    .read_raw = garrison_protection_read_raw
};

struct track_handler garrison_ii_protection_handler = {
    .write_raw = garrison_protection_write_raw,
    .read_raw = garrison_protection_read_raw
};

/*
 * This empty track is used by the protection check.  It reads in the track
 * while incrementing a counter unti the track is read.  The counter must be
 * less than 0xc0.  Reduced the total_bits to guarentee it will always be 
 * less than 0c0.  
 */

static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

static void *garrison_empty_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (!check_sequence(s, 3000, 0xff))
            continue;

        ti->total_bits = (ti->type == TRKTYP_garrison_ii_empty) ? 99627 : 99417;
        return memalloc(0);
    }

    return NULL;
}

static void garrison_empty_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (i = 0; i < 6000; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);
}

struct track_handler garrison_empty_handler = {
    .write_raw = garrison_empty_write_raw,
    .read_raw = garrison_empty_read_raw
};

struct track_handler garrison_ii_empty_handler = {
    .write_raw = garrison_empty_write_raw,
    .read_raw = garrison_empty_read_raw
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
