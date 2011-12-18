/*
 * disk/ikplus.c
 * 
 * Custom format as used by IK+ by System Studios / Archer Maclean.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8944,0x8944,0x8944 :: Sync
 *  u8  data[12*512]
 *  u16 crc_ccitt :: Over all track contents, in order
 * MFM encoding:
 *  Continuous, no even/odd split
 * 
 * TRKTYP_ikplus data layout:
 *  u8 sector_data[12*512]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static const uint8_t sync[3] = { 0x1a, 0x1a, 0x1a };

static void *ikplus_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t idx_off = s->index_offset - 15;
        uint16_t crc, mfm[ti->len+2];

        if ((uint16_t)s->word != 0x8944)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x89448944)
            continue;

        if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
            goto fail;
        mfm_decode_bytes(MFM_all, ti->len, mfm, block);

        crc = crc16_ccitt(sync, sizeof(sync), 0xffff);
        crc = crc16_ccitt(block, ti->len, crc);
        mfm_decode_bytes(MFM_all, 2, &mfm[ti->len], mfm);
        if (ntohs(mfm[0]) != crc)
            continue;

        ti->data_bitoff = idx_off;
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void ikplus_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t crc;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x89448944);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8944);

    tbuf_bytes(tbuf, SPEED_AVG, MFM_all, ti->len, ti->dat);

    crc = crc16_ccitt(sync, sizeof(sync), 0xffff);
    crc = crc16_ccitt(ti->dat, ti->len, crc);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, crc);
}

struct track_handler ikplus_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = ikplus_write_mfm,
    .read_mfm = ikplus_read_mfm
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
