/*
 * disk/star_trash.c
 *
 * Custom format as used on Star Trash by Rainbow Arts.
 *
 * Written in 2025 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 dat[ti->len/4]
 *  u16 crc16_ccitt
 * 
 * CRC is a duplicator's checksum: It is ignored by the game trackloader.
 *
 * TRKTYP_star_trash data layout:
 *  u8 sector_data[5888]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *star_trash_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        stream_start_crc(s);
        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
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

static void star_trash_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_start_crc(tbuf);

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    /* data */
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

    /* crc */
    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);

}

struct track_handler star_trash_handler = {
    .bytes_per_sector = 5888,
    .nr_sectors = 1,
    .write_raw = star_trash_write_raw,
    .read_raw = star_trash_read_raw
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
