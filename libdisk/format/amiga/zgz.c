/*
 * disk/zgz.c
 *
 * Custom format as used on Zestaw Gier Zręcznościowych by Fi PaxPol
 *
 * Written in 2025 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x89448944, Sync
 *  u32 header sig = checksum (5052 2CF0)
 *  u32 dat[ti->len/4]
 * 
 * Checksum is the sum of all u16 in the decoded data
 *
 * TRKTYP_zgz data layout:
 *  u8 sector_data[6144]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *zgz_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], hdr;
        uint16_t sum, csum;
        unsigned int i;
        char *block;

        if (s->word != 0x89448944)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        hdr = be32toh(hdr);
        csum = (uint16_t)hdr;

        if((uint16_t)(hdr >> 16) != 0x5052)
            continue;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += (uint16_t)(be32toh(dat[i]) >> 16) + (uint16_t)be32toh(dat[i]);
        }

        if (csum != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);

        return block;
    }

fail:
    return NULL;
}

static void zgz_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    uint16_t sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x89448944);

    for (i = sum = 0; i < ti->len/4; i++) {
        sum += (uint16_t)(be32toh(dat[i]) >> 16) + (uint16_t)be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0x50520000 | sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler zgz_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = zgz_write_raw,
    .read_raw = zgz_read_raw
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
