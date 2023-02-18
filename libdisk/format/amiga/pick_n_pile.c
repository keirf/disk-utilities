/*
 * disk/pick_n_pile.c
 *
 * Custom format as used by Pick'N Pile from UBI Soft
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 23 sectors back-to-back:
 *  u32 0x44894489 :: Sync
 *  u32 0x2AAAA888 :: sig
 *  u8 data[260] :: Even/odd blocks
 *  u16 0
 * 
 * data[0] contains the sector*4 in the high 16 and the tracknr/2
 * in the low 16
 * 
 * data[1] contains the checksum, which is the sum of the decoded
 * data.
 *
 * TRKTYP_pick_n_pile data layout:
 *  u8 sector_data[23][260]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *pick_n_pile_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int nr_valid_blocks = 0;

    block = memalloc(ti->nr_sectors*ti->bytes_per_sector);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t raw[2*ti->bytes_per_sector/4], sum;
        uint32_t dat[ti->bytes_per_sector/4];
        unsigned int sec, i;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding/sig */
        if (stream_next_bits(s, 32) == -1)
            continue;
        if (s->word != 0x2AAAA888)
            continue;

        /* Read and decode data. */
        if (stream_next_bytes(s, raw, 2*ti->bytes_per_sector) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, raw, &dat);

        /* checksum */
        sum = 0;
        for (i = 2; i < ti->bytes_per_sector/4; i++) {
            sum += be32toh(dat[i]);
        }

        if (sum != be32toh(dat[1]))
            continue;

        sec = (u_int8_t)(be32toh(dat[0]) >> 16)/4;

        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        memcpy(&block[sec*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
    }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void pick_n_pile_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector/4], sum;
    unsigned int i, sec;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2AAAA888);

        memcpy(dat, &ti->dat[sec*ti->bytes_per_sector], ti->bytes_per_sector);

        /* track and sector */
        dat[0] = be32toh(((sec*4) << 16) | (tracknr/2));

        /* checksum */
        sum = 0;
        for (i = 2; i < ti->bytes_per_sector/4; i++) {
            sum += be32toh(dat[i]);
        }
        dat[1] = be32toh(sum);

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector, &dat);

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    }
}

struct track_handler pick_n_pile_handler = {
    .bytes_per_sector = 260,
    .nr_sectors = 23,
    .write_raw = pick_n_pile_write_raw,
    .read_raw = pick_n_pile_read_raw
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
