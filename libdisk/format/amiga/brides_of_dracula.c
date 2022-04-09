/*
 * disk/brides_of_dracula.c
 * 
 * Custom format for The Brides of Dracula
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x48a1 :: Sync
 *  u16 0x5045
 *  u16 0xaaaa (0x6aaa for track 52 and 80)
 *  u32 data[1610/4]
 * 
 * Checksum is part of the decoded data
 *  (be32toh(dat[ti->len/4-4]) << 16) + (be32toh(dat[ti->len/4-3]) >> 16)
 * 
 * TRKTYP_brides_of_dracula data layout:
 *  u8 sector_data[5648]
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *brides_of_dracula_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*ti->len/4], dat[ti->len/4], i, csum, csum2;

        if ((uint16_t)s->word != 0x48a1)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5045)
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;

        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

        csum = 0;
        for (i = csum = 0; i < ti->len/4-4; i++){
            csum += (uint16_t)(be32toh(dat[i]) >> 16);
            csum += (uint16_t)(be32toh(dat[i]) & 0x0000FFFFuL);
        }

        csum2 = (be32toh(dat[ti->len/4-4]) << 16) + (be32toh(dat[ti->len/4-3]) >> 16);

        if (csum != csum2)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void brides_of_dracula_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x48a1);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5045);

    if (tracknr == 52 || tracknr == 80)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x6aaa);
    else
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xaaaa);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler brides_of_dracula_handler = {
    .bytes_per_sector = 5648,
    .nr_sectors = 1,
    .write_raw = brides_of_dracula_write_raw,
    .read_raw = brides_of_dracula_read_raw
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
