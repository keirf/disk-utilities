/*
 * disk/typhoon_thompson.c
 *
 * Custom format as used on Typhoon Thompson by Brøderbund.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4891 :: Sync
 *  u32 0x489144a9 :: Sync
 *  u32 csum  :: Even/odd words, AmigaDOS-style over header and data
 *  u32 track :: track number
 *  u32 dat[6144/4]
 *
 * TRKTYP_typhoon_thompson data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *typhoon_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, hdr, raw[2*ti->len/4], dat[ti->len/4];
        char *block;

        /* Scan for sync pattern. */
        if (s->word != 0x48914891)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0x489144a9)
            continue;
        ti->data_bitoff = s->index_offset_bc - 47;

        /* Read the checksum. */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* Read and validate the header longword. */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        if (be32toh(hdr) != tracknr)
            continue;

        /* Read and decode the data. */
        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

        /* Validate the checksum. */
        if (be32toh(csum) != (amigados_checksum(&hdr, 4)
                              ^ amigados_checksum(dat, ti->len)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void typhoon_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4891);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x489144a9);

    csum = htobe32(tracknr);
    csum = amigados_checksum(&csum, 4) ^ amigados_checksum(ti->dat, ti->len);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, tracknr);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, ti->dat);

}

struct track_handler typhoon_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = typhoon_write_raw,
    .read_raw = typhoon_read_raw
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
