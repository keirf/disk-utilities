/*
 * disk/universal_warrior.c
 *
 * Custom format as used on Universal Warrior by Zeppelin
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x3489 Sync
 *  u32 checksum
 *  u32 dat[ti->len/4]
 *
 * TRKTYP_universal_warrior data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *universal_warrior_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x3489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (be32toh(csum) != sum)
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

static void universal_warrior_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x3489);

    for (i = sum = 0; i < ti->len/4; i++) {
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
    
}

struct track_handler universal_warrior_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = universal_warrior_write_raw,
    .read_raw = universal_warrior_read_raw
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
