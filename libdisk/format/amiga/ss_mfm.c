/*
 * disk/ss_mfm.c
 * 
 * Custom format by Shaun Southern (Magnetic Fields / Gremlin) as used by
 * various Gremlin Graphics releases:
 *   Lotus I, II, and III
 *   Harlequin
 *   Zool 1 and 2
 *   ... and many more
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489,0x4489
 *  u16 0x5555
 *  u16 data[12*512/2]
 *  u16 csum
 *  u16 trk
 *  Checksum is sum of all decoded words
 *  Sides 0 and 1 of disk are inverted from normal.
 * MFM encoding:
 *  Alternating odd/even words
 * 
 * TRKTYP_ss_mfm data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *ss_mfm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat, csum = 0, trk;
        uint32_t idx_off = s->index_offset_bc - 15;

        if ((uint16_t)s->word != 0x4489)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44894489)
            continue;
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5555)
            continue;

        ti->data_bitoff = idx_off;

        for (i = 0; i < ti->nr_sectors*ti->bytes_per_sector/2; i++) {
            if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &block[i]);
            csum += be16toh(block[i]);
        }

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &dat);
        csum -= be16toh(dat);

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 2, raw, &dat);
        trk = be16toh(dat);

        if ((csum != 0) || (tracknr != (trk^1)))
            continue;

        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void ss_mfm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum = 0, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895555);

    for (i = 0; i < ti->nr_sectors*ti->bytes_per_sector/2; i++) {
        csum += be16toh(dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, be16toh(dat[i]));
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, csum);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 16, tracknr^1);
}

struct track_handler ss_mfm_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = ss_mfm_write_raw,
    .read_raw = ss_mfm_read_raw
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
