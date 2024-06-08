/*
 * disk/telexpress.c
 *
 * Custom format as used on Telexpress V2.0 by M
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x52a452a4 - SIG
 *  u32 x30 0x0
 *  u32 0x44a144a1
 *  u32 dat[ti->len/4]
 *
 * TRKTYP_telexpress data layout:
 *  u8 sector_data[6232]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *telexpress_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        
        if (s->word != 0x44894489)
            continue;
        
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word == 0x52a452a4)
            break;
    }

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44a144a1)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;


        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (sum != 0xefff218f)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 102000;
        return block;
    }

fail:
    return NULL;
}

static void telexpress_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x52a452a4);

    for (i = 0; i < 30; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44a144a1);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
}

struct track_handler telexpress_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = telexpress_write_raw,
    .read_raw = telexpress_read_raw
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
