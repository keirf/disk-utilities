/*
 * disk/novagen.c
 * 
 * Custom format as used by Novagen.
 * 
 * Written in 2020 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 sync+ :: 1-3 Sync Words
 *  u8  0x12,0x34,0x56,0x78,cyl
 *  u8  dat[0x1800]
 *  u8  csum_lo, csum_hi
 * 
 * TRKTYP_novagen data layout:
 *  u8 sector_data[0x1800]
 *  u8 sync_id
 */

#include <libdisk/util.h>
#include <private/disk.h>

const static uint16_t syncs[] = { 0xa89a, 0x8914, 0x4489 };

static void *novagen_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint8_t dat[0x1800*2], sum[2*2];
        uint16_t csum;
        uint8_t *block;
        unsigned int i, sync_id;

        for (sync_id = 0; sync_id < ARRAY_SIZE(syncs); sync_id++)
            if ((uint16_t)s->word == syncs[sync_id])
                goto found;
        continue;
    found:

        ti->data_bitoff = s->index_offset_bc - 15;

        /* This apes the header search in the game's trackloader. */
        for (i = 0; i < 16; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (mfm_decode_word(s->word) == 0x1234)
                goto found_header;
        }
        continue;

    found_header:
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0x5678)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint8_t)mfm_decode_word(s->word) != (tracknr>>1))
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

        block = memalloc(ti->len+1);
        memcpy(block, dat, ti->len);
        block[ti->len++] = sync_id;
        set_all_sectors_valid(ti);
        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 101100) ? 102200 : 100000;
        return block;
    }

fail:
    return NULL;
}

static void novagen_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, sync;
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    sync = syncs[dat[0x1800]];
    for (i = 0; i < 3; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0x12345678);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr>>1);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 0x1800, dat);

    for (i = csum = 0; i < 0x1800; i++) {
        uint8_t x = csum + dat[i];
        csum = ((csum & 0xff00) << 1) | (csum >> 15) | (x << 1);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)(csum>>0));
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)(csum>>8));
}

struct track_handler novagen_handler = {
    .bytes_per_sector = 0x1800,
    .nr_sectors = 1,
    .write_raw = novagen_write_raw,
    .read_raw = novagen_read_raw
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
