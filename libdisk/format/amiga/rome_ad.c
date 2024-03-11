/*
 * disk/rome_ad.c
 *
 * Custom format as used on Rome AD92 by Millennium
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 track number
 *  u32 checksum
 *  u32 dat[ti->len/4]
 *
 * TRKTYP_rome_ad data layout:
 *  u8 sector_data[6272]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t checksum(uint32_t data, uint32_t chk)
{
    uint64_t sum = 0;
    sum = (uint64_t)data + (uint64_t)chk;

    if ((sum >> 32) > 0)
        sum++;
    return (uint32_t)sum;
}

static void *rome_ad_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1], csum, sum, trk;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);

        if (tracknr != (uint8_t)be32toh(trk))
            continue;
        dat[ti->len/4] = 0xffff0000 & be32toh(trk);
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum = checksum(be32toh(dat[i]), sum);
        }

        if (be32toh(csum) != sum)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void rome_ad_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, dat[ti->len/4] | tracknr);

    for (i = sum = 0; i < ti->len/4; i++) {
        sum = checksum(be32toh(dat[i]), sum);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler rome_ad_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = rome_ad_write_raw,
    .read_raw = rome_ad_read_raw
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
