/*
 * disk/tracker.c
 * 
 * Custom format as used on Tracker by Mindware/Rainbird, released in 1988.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489,0x4489,0x4489 :: Sync
 *  u32 header[2]  :: Even/odd
 *  u32 csum[2]    :: Even/odd
 *  u32 zero[2] :: Even/odd
 *  u8  data[11][512][2] :: Even/odd blocks
 * AmigaDOS-style checksum. Header is 0xff0000ff | (tracknr << 16).
 * 
 * TRKTYP_tracker data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *tracker_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*512/4], dat[11*512/4], csum;
        unsigned int sec;
        char *block;

        if (s->word != 0x44894489)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 63;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, dat);
        if (be32toh(dat[0]) != (0xff0000ffu | ((tracknr/2)-1)<<16))
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
        csum = be32toh(csum);

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, dat);
        if (be32toh(dat[0]) != 0)
            continue;

        for (sec = 0; sec < ti->nr_sectors; sec++) {
            if (stream_next_bytes(s, raw, 2*512) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 512, raw, &dat[sec*512/4]);
        }

        if (csum != amigados_checksum(dat, sizeof(dat)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void tracker_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr, csum;
    unsigned int sec;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    hdr = 0xff0000ffu | (((tracknr/2)-1)<<16);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    csum = amigados_checksum(ti->dat, ti->len);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);

    for (sec = 0; sec < ti->nr_sectors; sec++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 512, &ti->dat[sec*512]);
}

struct track_handler tracker_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = tracker_write_raw,
    .read_raw = tracker_read_raw
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
