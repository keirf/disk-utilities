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
#include <private/disk.h>

static void *firebird_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {

        uint32_t idx_off = s->index_offset_bc - 31;
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
            if (mfm_decode_word((uint16_t)s->word) != 0xff)
                continue;
        } else if (ti->type == TRKTYP_afterburner_data) {
            if (stream_next_bytes(s, dat, 6) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm, 3, dat, dat);
            if ((dat[0] != 0x41) || (dat[1] != 0x42) ||
                (dat[2] != (tracknr/2)))
                continue;
        }

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(bc_mfm, ti->len, dat, block);
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

static void firebird_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    if (ti->type == TRKTYP_ikplus)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xf72a);

    tbuf_start_crc(tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x89448944);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8944);

    if (ti->type == TRKTYP_firebird) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
    } else if (ti->type == TRKTYP_afterburner_data) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x4142);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr/2);
    }

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, ti->len, ti->dat);

    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler firebird_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = firebird_write_raw,
    .read_raw = firebird_read_raw
};

struct track_handler ikplus_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = firebird_write_raw,
    .read_raw = firebird_read_raw
};

struct track_handler afterburner_data_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = firebird_write_raw,
    .read_raw = firebird_read_raw
};

/*
 * Custom formats as used by Quartz
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x8944, 0x8944 :: Sync (TRKTYP_quartz_a)
 *  u16 0x8944, 0xa92a, 0x8944 :: Sync (TRKTYP_quartz_b)
 *  u8  data[6168]
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[6168]
 */

static void *firebird_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t sync;
        uint8_t raw[2], dat[ti->len], sum;
        unsigned int i;
        char *block;

        sync = (ti->type == TRKTYP_quartz_a) ? 0x89448944 : 0x8944a92a;

        if (s->word != sync)
                continue;

        if (ti->type == TRKTYP_quartz_b){
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if ((uint16_t)s->word != 0x8944)
                continue;
            ti->data_bitoff = s->index_offset_bc - 47;
         } else
            ti->data_bitoff = s->index_offset_bc - 31;


        for (i = sum = 0; i < ti->len; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &dat[i]);
        }

        if (dat[2] != tracknr/2)
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

static void firebird_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;
    uint8_t *dat = (uint8_t *)ti->dat;

    if (ti->type == TRKTYP_quartz_a)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x89448944);
    else {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x8944a92a);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8944);
    }

    for (i = 0; i < ti->len; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[i]);
}

struct track_handler quartz_a_handler = {
    .bytes_per_sector = 6168,
    .nr_sectors = 1,
    .write_raw = firebird_b_write_raw,
    .read_raw = firebird_b_read_raw
};
struct track_handler quartz_b_handler = {
    .bytes_per_sector = 6168,
    .nr_sectors = 1,
    .write_raw = firebird_b_write_raw,
    .read_raw = firebird_b_read_raw
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
