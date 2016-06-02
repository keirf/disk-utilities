/*
 * disk/savage.c
 *
 * Custom format as used by Savage from MicroPlay/Firebird:
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 11 sectors back-to-back:
 *  u16 0x4489,0x4489 :: Sync
 *  u32 header :: Even/odd
 *  u32 header_csum :: Even/odd
 *  u32 data_csum :: Even/odd
 *  u8  data[512] :: Even/odd blocks
 *  u32 zero :: Even/odd
 *  Header is 0xff000000u | track number << 16 | current sector << 8 |
 *  (total sectors - current sector)
 *
 * TRKTYP_savage data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *savage_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, hdr, zero;
        uint32_t raw[2], raw2[2*ti->bytes_per_sector/4];
        uint32_t dat[ti->nr_sectors*ti->bytes_per_sector/4];
        unsigned int sec;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (sec = 0; sec < ti->nr_sectors; sec++) {

            /* Validate the sync. */
            if (s->word != 0x44894489)
                break;

            /* Read and validate header longword. */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
            if (be32toh(hdr) != (0xff000000u | tracknr<<16 | sec<<8
                                 | (ti->nr_sectors-sec)))
                break;

            /* Read and validate header checksum. */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
            if (be32toh(csum) != amigados_checksum(&hdr, 4))
                break;

            /* Read data checksum. */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            /* Read and decode data. */
            if (stream_next_bytes(s, raw2, 2*ti->bytes_per_sector) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector,
                raw2, &dat[sec*ti->bytes_per_sector/4]);

            /* Validate the checksum. */
            if (be32toh(csum)
                != amigados_checksum(&dat[sec*ti->bytes_per_sector/4],
                                     ti->bytes_per_sector))
                break;

            /* Read and validate the sector gap. */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &zero);
            if (be32toh(zero) != 0)
                break;

            /* Read the next sync. */
            if (stream_next_bits(s, 32) == -1)
                goto fail;
        }

        if (sec != ti->nr_sectors)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void savage_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr;
    unsigned int sec;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

        /* header */
        hdr = 0xff000000u | tracknr<<16 | sec<<8 | (ti->nr_sectors-sec);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

        /* header checksum */
        hdr = htobe32(hdr);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                  amigados_checksum(&hdr, 4));

        /* data checksum */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                  amigados_checksum(&ti->dat[sec*ti->bytes_per_sector],
                                    ti->bytes_per_sector));

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector,
                   &ti->dat[sec*ti->bytes_per_sector]);

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    }
}

struct track_handler savage_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = savage_write_raw,
    .read_raw = savage_read_raw
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
