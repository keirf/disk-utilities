/*
 * disk/manic_miner.c
 *
 * Custom format as used on Manic Miner from Software Projects
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 :: Sync : one of the 4 syncs - array index = tracknr % 4
 *         {0x8944, 0x44a2, 0x2251, 0x9128}
 *  u16 0xaaaa
 * 
 *  loop 12 sectors
 *      u32 checksum
 *      u32 dat[ti->bytes_per_sector/4]

 *
 * TRKTYP_manic_miner data layout:
 *  u8 sector_data[12*512]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

const static uint16_t syncs[] = {
    0x8944, 0x44a2, 0x2251, 0x9128
};

static void *manic_miner_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int i, j, nr_valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        
        /* one of 4 syncs tracknr % 4 to get the correct sync for the track */
        if ((uint16_t)s->word != syncs[tracknr%4])
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding - never checked */
        if (stream_next_bits(s, 16) == -1)
            goto done;

        /* decode data */
        for (j = 0; j < ti->nr_sectors; j++) {
            uint32_t raw[2], dat[ti->bytes_per_sector/4], csum, sum;

            /* checksum per sector */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto done; 
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            /* data */
            sum = 0xffffffff;
            for (i = 0; i < ti->bytes_per_sector/4; i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    goto done;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
                sum -= be32toh(dat[i]);
            }

            if (be32toh(csum) == sum && !is_valid_sector(ti, j)) {
                memcpy(&block[j*ti->bytes_per_sector], dat, ti->bytes_per_sector);
                set_sector_valid(ti, j);
                nr_valid_blocks++;
            }
        }

        if (nr_valid_blocks == 0)
            goto done;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return block;
    }

done:
    free(block);
    return NULL;

}

static void manic_miner_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i, j;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[tracknr%4]);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xaaaa);

    /* data and checksum */
    for (j = 0; j < ti->nr_sectors; j++) {
        sum = 0xffffffff;
        for (i = 0; i < ti->bytes_per_sector/4; i++) {
            sum -= be32toh(dat[i+j*(ti->bytes_per_sector/4)]);
        }
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
        for (i = 0; i < ti->bytes_per_sector/4; i++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 
                be32toh(dat[i+j*(ti->bytes_per_sector/4)]));
        }
    }
}

struct track_handler manic_miner_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = manic_miner_write_raw,
    .read_raw = manic_miner_read_raw
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
