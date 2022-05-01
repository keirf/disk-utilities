/*
 * disk/pinball_fantasies.c
 * 
 * Custom format for Pinball Fantasies from 21st Century
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
 * TRKTYP_pinball_fantasies data layout:
 *  u8 sector_data[6306]
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *pinball_fantasies_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1], csum, sum;
        unsigned int i;

        if (s->word != 0x21122112)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x448A)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum ^= raw[0];
            sum ^= raw[1];
        }
        sum &= 0x55555555;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x54555251)
            continue;
        
        if (sum != csum)
            goto fail;

        dat[ti->len/4] = csum;

        if (tracknr == 3 && ti->type == TRKTYP_pinball_fantasies_tables)
            ti->total_bits = 102200;
        else
            ti->total_bits = 105500;
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void pinball_fantasies_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x21122112);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x448A);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[ti->len/4]));
    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x54555251);
}

struct track_handler pinball_fantasies_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = pinball_fantasies_write_raw,
    .read_raw = pinball_fantasies_read_raw
};

struct track_handler pinball_fantasies_tables_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = pinball_fantasies_write_raw,
    .read_raw = pinball_fantasies_read_raw
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
