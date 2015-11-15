/*
 * disk/summer_games.c
 * 
 * Custom format as used by "The Games: Summer Edition" by Epyx / US Gold.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,trknr,0x00,csum :: Even/Odd long
 *  u32 data[12][500/4] :: Even/Odd longs
 * Checksum is EOR.B over all data
 * 
 * TRKTYP_summer_games data layout:
 *  u8 sector_data[12][500]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *summer_games_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2], dat[ti->nr_sectors][ti->bytes_per_sector/4];
        uint32_t csum, hdr;
        unsigned int i, j;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw_dat, &hdr);
        hdr = be32toh(hdr);
        if ((hdr >> 8) != (0xff0000|(tracknr<<8)))
            continue;

        csum = 0;
        for (i = 0; i < ti->nr_sectors; i++) {
            for (j = 0; j < 125; j++) {
                if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
                    goto fail;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw_dat, &dat[i][j]);
                csum ^= dat[i][j];
            }
        }

        csum = (uint8_t)((csum >> 24) ^ (csum >> 16) ^ (csum >> 8) ^ csum);
        if ((uint8_t)hdr != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void summer_games_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    csum = 0;
    for (i = 0; i < ti->nr_sectors*ti->bytes_per_sector/4; i++)
        csum ^= dat[i];
    csum = (uint8_t)((csum >> 24) ^ (csum >> 16) ^ (csum >> 8) ^ csum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              0xff000000 | (tracknr<<16) | csum);

    for (i = 0; i < ti->nr_sectors*ti->bytes_per_sector/4; i++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 4, &dat[i]);
}

struct track_handler summer_games_handler = {
    .bytes_per_sector = 500,
    .nr_sectors = 12,
    .write_raw = summer_games_write_raw,
    .read_raw = summer_games_read_raw
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
