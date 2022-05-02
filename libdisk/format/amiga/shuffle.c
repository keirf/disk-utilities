/*
 * disk/shuffle.c
 *
 * Custom format as used on Shuffle by Tale.
 *
 * Written in 2022 by Keith Krellwitz & Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u8  0xA1 (4489 Sync)
 *  u16 0
 *  u32 dat[6300/4]
 *  u16 crc16_ccitt
 * 
 * CRC is a duplicator's checksum: It is ignored by the game trackloader.
 *
 * TRKTYP_shuffle data layout:
 *  u8 sector_data[6300]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *shuffle_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        /* sync */
        if ((s->word>>16) != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;
        stream_start_crc(s);

        /* pad */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0)
            continue;

        /* data */
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        /* crc */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;
       
        ti->total_bits = 102200;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void shuffle_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_start_crc(tbuf);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler shuffle_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = shuffle_write_raw,
    .read_raw = shuffle_read_raw
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
