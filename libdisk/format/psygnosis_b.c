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
#include "../private.h"

#include <arpa/inet.h>

static void *psygnosis_b_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    unsigned int j, k, valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<6)-1))) {

        uint16_t raw_dat[6*513];
        uint32_t idx_off, nr_valid = 0;

        if ((uint16_t)s->word != 0x4489)
            continue;

        idx_off = s->index_offset - 15;

        if (stream_next_bits(s, 32) == -1)
            goto done;

        if (s->word != 0x552aaaaa)
            continue;

        for (j = 0; j < sizeof(raw_dat)/2; j++) {
            uint32_t dat;
            if (stream_next_bytes(s, &dat, 4) == -1)
                goto done;
            mfm_decode_bytes(MFM_even_odd, 2, &dat, &raw_dat[j]);
        }

        for (j = 0; j < 6; j++) {
            uint16_t *sec = &raw_dat[j*513];
            uint16_t csum = ntohs(*sec++), c = 0;
            for (k = 0; k < 512; k++)
                c += ntohs(sec[k]);
            if (c == csum) {
                memcpy(&block[j*1024], sec, 1024);
                valid_blocks |= 1u << j;
                nr_valid++;
            }
        }

        if (nr_valid)
            ti->data_bitoff = idx_off;
    }

done:
    if (valid_blocks == 0) {
        free(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;

    return block;
}

static void psygnosis_b_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i, j;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0xf000);

    for (i = 0; i < 6; i++) {
        uint16_t csum = 0;
        for (j = 0; j < 512; j++)
            csum += ntohs(dat[j]);
        if (!(ti->valid_sectors & (1u << i)))
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 16, csum);
        for (j = 0; j < 512; j++, dat++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 16, ntohs(*dat));
    }
}

struct track_handler psygnosis_b_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 6,
    .write_mfm = psygnosis_b_write_mfm,
    .read_mfm = psygnosis_b_read_mfm
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
