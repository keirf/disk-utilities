/*
 * disk/deliverance.c
 * 
 * Custom format for Deliverance from 21st Century
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x21122112 :: Sync
 *  u16 0x5245
 *  u16 disk number (1 or 2)
 *  u16 tracknr
 *  u32 data[18a2] last 2 byte are only used by the checksum
 * 
 * Checksum is calculated from the raw data and should always be 0
 * 
 * TRKTYP_deliverance data layout:
 *  u8 sector_data[6306]
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *deliverance_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint8_t raw[2], dat[ti->bytes_per_sector+1];
        unsigned int i, j;
        uint16_t ctrack, cdisk, craw[2];
        uint32_t rdat[ti->bytes_per_sector/2], csum;

        if (s->word != 0x21122112)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5245)
            continue;

        if (stream_next_bytes(s, craw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &cdisk);
        dat[ti->bytes_per_sector] = (uint8_t)be16toh(cdisk);

        if (stream_next_bytes(s, craw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &ctrack);

        if (be16toh(ctrack) != tracknr)
            continue;

        for (i = j = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &dat[i]);
            if (i > 0 && i % 2) {
                rdat[j] = rdat[j] | raw[0] << 8 | raw[1];
                j++;
            }
            else
                rdat[j] = raw[0] << 24 | raw[1] << 16;
        }

        for (i = csum = 0; i < ti->bytes_per_sector/2; i++) {
            csum ^= rdat[i];
        }
        csum &= 0x55555555;
        if (csum != 0)
            continue;

        ti->total_bits = 105500;
        block = memalloc(ti->len+1);
        memcpy(block, dat, ti->len+1);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void deliverance_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x21122112);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5245);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, dat[ti->bytes_per_sector]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, tracknr);

    for (i = 0; i < ti->bytes_per_sector; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[i]);
}

struct track_handler deliverance_handler = {
    .bytes_per_sector = 6306,
    .nr_sectors = 1,
    .write_raw = deliverance_write_raw,
    .read_raw = deliverance_read_raw
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
