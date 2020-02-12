/*
 * disk/mercenary.c
 * 
 * Custom format as used on Mercenary by Paul Woakes (Novagen).
 * 
 * Written in 2020 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0xa89a,0xa89a :: Sync
 *  u8  0x04,0x12,0x34,0x56,0x78,cyl
 *  u8  dat[0x1800]
 *  u8  csum_lo, csum_hi
 * 
 * TRKTYP_mercenary data layout:
 *  u8 sector_data[0x1800]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint8_t exp[5] = { 0x04, 0x12, 0x34, 0x56, 0x78 };

static void *mercenary_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint8_t dat[0x1800*2], hdr[6*2], sum[2*2];
        uint16_t csum;
        uint8_t *block;
        unsigned int i;

        if (s->word != 0xa89aa89a)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, hdr, 6*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 6, hdr, hdr);
        if (memcmp(exp, hdr, 5) || (hdr[5] != tracknr>>1))
            continue;
        
        if (stream_next_bytes(s, dat, 0x1800*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 0x1800, dat, dat);
        for (i = csum = 0; i < 0x1800; i++) {
            uint8_t x = csum + dat[i];
            csum = ((csum & 0xff00) << 1) | (csum >> 15) | (x << 1);
        }

        if (stream_next_bytes(s, sum, 2*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 2, sum, sum);
        if (csum != (sum[0] | (sum[1]<<8)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 102500;
        return block;
    }

fail:
    return NULL;
}

static void mercenary_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum;
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa89aa89a);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, sizeof(exp), (void *)exp);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr>>1);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 0x1800, dat);

    for (i = csum = 0; i < 0x1800; i++) {
        uint8_t x = csum + dat[i];
        csum = ((csum & 0xff00) << 1) | (csum >> 15) | (x << 1);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)(csum>>0));
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)(csum>>8));
}

struct track_handler mercenary_handler = {
    .bytes_per_sector = 0x1800,
    .nr_sectors = 1,
    .write_raw = mercenary_write_raw,
    .read_raw = mercenary_read_raw
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
