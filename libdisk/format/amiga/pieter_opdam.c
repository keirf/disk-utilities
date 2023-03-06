/*
 * disk/pieter_opdam.c
 *
 * Custom format as used on Mugician from Thalamus:
 * 
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x48914891 :: Sync
 *  u32 dat[ti->len/4]
 *
 *  The checksum is the sum of all bytes and is checked in code
 *  for the total = 0xed
 * 
 * The protection checks several decoded long words and if that
 * passes then the checksum calculation is done and must equal 0xed
 * 
 * TRKTYP_pieter_opdam data layout:
 *  u8 sector_data[0x1810]
 * 
 * 
 * Excerpt from the protection track
 * 
 * THIS COPY-PROTECTION IZ DONE BY PIETER 'VENOMWING' OPDAM OF 
 * SOFTEYES!!!!!!!!!!
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *pieter_opdam_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t dat[2*ti->len/4];
        uint8_t sum;
        char *block;
        unsigned int i;

        /* sync */
        if (s->word != 0x48914891)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        for (i = sum = 0; i < ti->len/4; i++) {
            sum += (uint8_t)(be32toh(dat[i]) >> 24);
            sum += (uint8_t)(be32toh(dat[i]) >> 16);
            sum += (uint8_t)(be32toh(dat[i]) >> 8);
            sum += (uint8_t)be32toh(dat[i]);
        }

        if (sum != 0xed)
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }
fail:

    return NULL;
}

static void pieter_opdam_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x48914891);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler pieter_opdam_handler = {
    .bytes_per_sector = 6160,
    .nr_sectors = 1,
    .write_raw = pieter_opdam_write_raw,
    .read_raw = pieter_opdam_read_raw
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
