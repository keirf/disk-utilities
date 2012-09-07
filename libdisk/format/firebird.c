/*
 * disk/firebird.c
 * 
 * Custom formats as used by:
 *   After Burner (Software Studios / Argonaut)
 *   IK+ (Software Studios / Archer Maclean)
 *   Virus (Firebird / David Braben)
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0xf72a (TRKTYP_ikplus only)
 *  u16 0x8944,0x8944,0x8944 :: Sync
 *  u8  0xff (TRKTYP_firebird only)
 *  u8  0x41,0x42,cyl (TRKTYP_afterburner_data only)
 *  u8  data[12*512]
 *  u16 crc_ccitt :: Over all track contents, in order
 * MFM encoding:
 *  Continuous, no even/odd split
 * 
 * TRKTYP_* data layout:
 *  u8 sector_data[12*512]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *firebird_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t idx_off = s->index_offset - 31;
        uint8_t dat[2*(ti->len+2)];

        if (s->word != 0x89448944)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0x89448944)
            continue;

        if (ti->type == TRKTYP_firebird) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != 0xff)
                continue;
        } else if (ti->type == TRKTYP_afterburner_data) {
            if (stream_next_bytes(s, dat, 6) == -1)
                goto fail;
            mfm_decode_bytes(MFM_all, 3, dat, dat);
            if ((dat[0] != 0x41) || (dat[1] != 0x42) ||
                (dat[2] != (tracknr/2)))
                continue;
        }

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(MFM_all, ti->len, dat, block);
        ti->data_bitoff = idx_off;
        if (ti->type == TRKTYP_ikplus)
            ti->data_bitoff -= 2*16; /* IK+ has a pre-sync header */
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void firebird_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    if (ti->type == TRKTYP_ikplus)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0xf72a);

    tbuf_start_crc(tbuf);

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x89448944);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8944);

    if (ti->type == TRKTYP_firebird) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0xff);
    } else if (ti->type == TRKTYP_afterburner_data) {
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, 0x4142);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, tracknr/2);
    }

    tbuf_bytes(tbuf, SPEED_AVG, MFM_all, ti->len, ti->dat);

    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler firebird_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = firebird_write_mfm,
    .read_mfm = firebird_read_mfm
};

struct track_handler ikplus_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = firebird_write_mfm,
    .read_mfm = firebird_read_mfm
};

struct track_handler afterburner_data_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = firebird_write_mfm,
    .read_mfm = firebird_read_mfm
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
