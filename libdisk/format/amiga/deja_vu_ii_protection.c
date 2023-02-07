/*
 * disk/deja_vu_ii_protection.c
 *
 * Custom format as used on Deja Vu II by Mindscape
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0xA245 Sync
 *  u32 dat[ti->len/4]
 *
 * The sum of the raw data must equal 0xEA6DB480
 *
 * TRKTYP_deja_vu_ii_protection data layout:
 *  u8 sector_data[6200]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *deja_vu_ii_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;
        ti->data_bitoff = s->index_offset_bc - 31;
        if ((uint16_t)s->word!= 0xA245)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum -= be32toh(raw[0]);
            sum -= be32toh(raw[1]);
        }
  
        if (sum != 0xEA6DB480)
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

static void deja_vu_ii_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xA245);
    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler deja_vu_ii_protection_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = deja_vu_ii_protection_write_raw,
    .read_raw = deja_vu_ii_protection_read_raw
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
