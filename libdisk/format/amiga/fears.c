/*
 * disk/fears.c
 *
 * Custom format as used on Fears by Manyk
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 :: Sync Even tracks 0x89448944 - Odd tracks 0x94489448
 *  u32 0xaaaaaaaa
 *  u32 0xaaaaaaaa
 *  u32 dat[8] :: Sector checksums
 *  u32 dat[8*748] :: data
 * 
 * Sector checksums are decoded and stored in an array. The 
 * decoded data is then eor'd over the 8 sector checksums.
 * 
 * 
 * TRKTYP_fears data layout:
 *  u8 sector_data[8*748]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *fears_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int i, j;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], cdat[8], dat[ti->len/4];
        uint32_t sdat[] = {0,0,0,0,0,0,0,0};


        if (s->word != (tracknr % 2 == 1 ? 0x94489448 : 0x89448944))
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding - never checked */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (stream_next_bits(s, 32) == -1)
            goto fail;

        /* decode sector checksums*/
        for (i = 0; i < 8; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &cdat[i]);
        }

        /* decode data */
        for (i = 0; i < ti->bytes_per_sector/4; i++) {
            for (j = 0; j < ti->nr_sectors; j++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    goto fail;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i*8+j]);
                sdat[j] ^= dat[i*8+j];
            }
        }
        
        for (j = 0; j < ti->nr_sectors; j++) {
            if (cdat[j] != sdat[j])
                goto fail;
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void fears_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    uint32_t sdat[] = {0,0,0,0,0,0,0,0};
    unsigned int i, j;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 
        (tracknr % 2 == 1 ? 0x94489448 : 0x89448944));

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    /* calculate checksums */
    for (i = 0; i < ti->bytes_per_sector/4; i++) {
        for (j = 0; j < ti->nr_sectors; j++) {
            sdat[j] ^= dat[i*8+j];
        }
    }

    /* write sector checksums */
    for (j = 0; j < ti->nr_sectors; j++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(sdat[j]));

    /* data */
    for (i = 0; i < ti->bytes_per_sector/4; i++) {
        for (j = 0; j < ti->nr_sectors; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 
                be32toh(dat[i*8+j]));
        }
    }

}

struct track_handler fears_handler = {
    .bytes_per_sector = 748,
    .nr_sectors = 8,
    .write_raw = fears_write_raw,
    .read_raw = fears_read_raw
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
