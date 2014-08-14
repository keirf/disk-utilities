/*
 * disk/psygnosis_b.c
 * 
 * Custom format as used by various Psygnosis releases:
 *   Amnios (Disk 2)
 *   Aquaventura (Disk 2)
 *   Lemmings
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x552a,0xaaaaa :: Sync
 *  6 back-to-back sectors (no gaps)
 * Decoded sector:
 *  u16 csum       :: sum of all 16-bit data words
 *  u16 data[512]
 * MFM encoding of sectors:
 *  u16 data -> u16 mfm_even,mfm_odd (i.e., sequence of interleaved e/o words)
 * Timings:
 *  Despite storing 6kB of data, minimal metadata means this is not stored
 *  on a long track. Cell timing is 2us as usual.
 * 
 * TRKTYP_psygnosis_b data layout:
 *  u8 sector_data[6][1024]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *psygnosis_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int j, k, nr_valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint16_t raw_dat[6*513];
        uint32_t idx_off, new_valid = 0;

        if ((uint16_t)s->word != 0x4489)
            continue;

        idx_off = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto done;

        if (s->word != 0x552aaaaa)
            continue;

        for (j = 0; j < sizeof(raw_dat)/2; j++) {
            uint32_t dat;
            if (stream_next_bytes(s, &dat, 4) == -1)
                goto done;
            mfm_decode_bytes(bc_mfm_even_odd, 2, &dat, &raw_dat[j]);
        }

        for (j = 0; j < 6; j++) {
            uint16_t *sec = &raw_dat[j*513];
            uint16_t csum = be16toh(*sec++), c = 0;
            for (k = 0; k < 512; k++)
                c += be16toh(sec[k]);
            if ((c == csum) && !is_valid_sector(ti, j)) {
                memcpy(&block[j*1024], sec, 1024);
                set_sector_valid(ti, j);
                nr_valid_blocks++;
                new_valid++;
            }
        }

        if (new_valid)
            ti->data_bitoff = idx_off;
    }

done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    return block;
}

static void psygnosis_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xf000);

    for (i = 0; i < 6; i++) {
        uint16_t csum = 0;
        for (j = 0; j < 512; j++)
            csum += be16toh(dat[j]);
        if (!is_valid_sector(ti, i))
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
        for (j = 0; j < 512; j++, dat++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(*dat));
    }
}

struct track_handler psygnosis_b_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 6,
    .write_raw = psygnosis_b_write_raw,
    .read_raw = psygnosis_b_read_raw
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
