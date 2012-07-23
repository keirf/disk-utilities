/*
 * disk/ikplus.c
 * 
 * Custom format as used by IK+ by System Studios / Archer Maclean.
 * Also variant used by Virus by Firebird / David Braben.
 * 
 * These may be members of a more general family of formats. If so this file
 * will be generalised further as appropriate.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0xf72a (TRKTYP_ikplus only)
 *  u16 0x8944,0x8944,0x8944 :: Sync
 *  u8  0xff (TRKTYP_virus only)
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

static void *ikplus_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t idx_off = s->index_offset - 31;
        uint16_t mfm[ti->len+2];

        if (s->word != 0x89448944)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0x89448944)
            continue;

        if (ti->type == TRKTYP_virus) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != 0xff)
                continue;
        }

        if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(MFM_all, ti->len, mfm, block);
        ti->data_bitoff = idx_off;
        if (ti->type == TRKTYP_ikplus)
            ti->data_bitoff -= 2*16; /* IK+ has a pre-sync header */
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

    if (ti->type == TRKTYP_ikplus)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0xf72a);
    tbuf_start_crc(tbuf);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x89448944);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8944);
    if (ti->type == TRKTYP_virus)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0xff);

    tbuf_bytes(tbuf, SPEED_AVG, MFM_all, ti->len, ti->dat);

    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler ikplus_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = ikplus_write_mfm,
    .read_mfm = ikplus_read_mfm
};

struct track_handler virus_handler = {
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
