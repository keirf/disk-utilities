/*
 * disk/back_future3.c
 *
 * Custom format as used on Back to the Future III from Mirrorsoft.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 0x552524a4
 *  u32 0x554a4945
 *  u32 dat[6144/4]
 *  u32 checksum
 *
 * The checksum does not match on several tracks on each disk.  The
 * stored checksum is actually the checksum for the previous track.
 * This has been confirmed on 3 different versions of the game.
 *
 * One version uses manual protection rather than using a copylock track.
 *
 *
 * TRKTYP_back_future3 data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>


const static unsigned int track_array[] = {25, 27, 38, 43, 49,
    56, 63, 66, 68, 74, 80, 82, 84, 87, 121, 124, 152, 155, 157};

static unsigned int find_track(int trck)
{
    int i;
    for (i = 0; i < sizeof(track_array)/sizeof(int); i++)
        if (track_array[i] == trck)
            return trck;
    return 0;
}

static void *back_future3_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x552524a4)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x554a4945)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            if ( i < ti->len/4 - 1)
                sum += be32toh(dat[i]);
        }

        /* Only verify the checksum on those tracks that are not
         * listed in the track_array*/
        if (find_track(tracknr) == 0)
            if (sum != be32toh(dat[i-1]))
                continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}



static void back_future3_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x552524a4);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x554a4945);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler back_future3_handler = {
    .bytes_per_sector = 6148,
    .nr_sectors = 1,
    .write_raw = back_future3_write_raw,
    .read_raw = back_future3_read_raw
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
