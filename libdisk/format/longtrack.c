/*
 * disk/longtrack.c
 * 
 * Detect various custom long protection tracks.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * TRKTYP_* data layout:
 *  No data (all track formats are fixed format with no key/real data)
 */

#include <libdisk/util.h>
#include "../private.h"

static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_bits(MFM_all, s->word) != byte)
            break;
    }
    return !nr;
}

static int check_length(struct stream *s, unsigned int min_bits)
{
    stream_next_index(s);
    return (s->track_bitlen >= min_bits);
}

/*
 * TRKTYP_protec_longtrack: PROTEC protection track, used on many releases
 *  u16 0x4454
 *  u8 0x33 (encoded in-place, 1000+ times, to track gap)
 *  Track is checked to be >= 107200 bits long
 *  Specifically, protection checks for >= 6700 mfm words gap between
 *  successive sync marks. Track contents are not otherwise checked or tested.
 */

static void *protec_longtrack_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset - 31;
        if ((s->word != 0x4454a525) || !check_sequence(s, 1000, 0x33))
            continue;
        if (!check_length(s, 107200))
            break;
        ti->total_bits = 110000; /* long enough */
        return memalloc(0);
    }

    return NULL;
}

static void protec_longtrack_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4454);
    for (i = 0; i < 6000; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x33);
}

struct track_handler protec_longtrack_handler = {
    .write_mfm = protec_longtrack_write_mfm,
    .read_mfm = protec_longtrack_read_mfm
};

/*
 * TRKTYP_gremlin_longtrack: Lotus I/II, + many others
 *  u16 0x4124,0x4124
 *  Rest of track is (MFM-encoded) zeroes, and/or unformatted garbage.
 *  The contents are never checked, only successive sync marks are scanned for.
 * 
 *  Track is checked to be >= 102400 bits long.
 *  Specifically, protection checks for >= 6400 mfm words gap between
 *  successive sync marks. Track contents are not otherwise checked or tested.
 * 
 *  Track is typically ~105500 bits long.
 */

static void *gremlin_longtrack_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset - 31;
        if ((s->word != 0x41244124) || !check_sequence(s, 8, 0x00))
            continue;
        if (ti->type != TRKTYP_tiertex_longtrack)
            ti->total_bits = 105500;
        return memalloc(0);
    }

    return NULL;
}

static void gremlin_longtrack_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x41244124);
    for (i = 0; i < (ti->total_bits/16)-250; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
}

struct track_handler gremlin_longtrack_handler = {
    .write_mfm = gremlin_longtrack_write_mfm,
    .read_mfm = gremlin_longtrack_read_mfm
};

/*
 * TRKTYP_tiertex_longtrack: Strider II
 *  A variant of the Gremlin long track, checks 99328 <= x <= 103680 bits long.
 *  Specifically, the variant checks 6208 <= x <= 6480 mfm words gap between
 *  successive sync marks. Track contents are not otherwise checked or tested.
 * 
 *  Track is actually ~100150 bits long (normal length!).
 */

struct track_handler tiertex_longtrack_handler = {
    .write_mfm = gremlin_longtrack_write_mfm,
    .read_mfm = gremlin_longtrack_read_mfm
};

/*
 * TRKTYP_crystals_of_arborea_longtrack: Crystals Of Arborea
 *  u16 0xa144 :: sync
 *  u8[] "ROD0" (encoded MFM_all)
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104128 bits long (track is ~110000 bits long)
 *  Specifically, protection checks for > 6500 0xaaaa/0x5555 mfm words
 *  starting 12 bytes into the DMA buffer (i.e., 12 bytes after the sync)
 */

static void *crystals_of_arborea_longtrack_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t raw[2];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset - 15;
        if (s->word != 0xaaaaa144)
            continue;
        stream_next_bytes(s, raw, 8);
        mfm_decode_bytes(MFM_all, 4, raw, raw);
        if (be32toh(raw[0]) != 0x524f4430) /* "ROD0" */
            continue;
        if (!check_sequence(s, 6500, 0x00))
            continue;
        if (!check_length(s, 104128))
            break;
        ti->total_bits = 110000;
        return memalloc(0);
    }

    return NULL;
}

static void crystals_of_arborea_longtrack_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0xa144);
    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 32, 0x524f4430); /* "ROD0" */
    for (i = 0; i < 6550; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
}

struct track_handler crystals_of_arborea_longtrack_handler = {
    .write_mfm = crystals_of_arborea_longtrack_write_mfm,
    .read_mfm = crystals_of_arborea_longtrack_read_mfm
};

/*
 * TRKTYP_infogrames_longtrack: Hostages, Jumping Jack Son, and others
 *  u16 0xa144 :: sync
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104160 bits long (track is ~105500 bits long)
 *  Specifically, protection checks for > 13020 0xaa mfm bytes, starting from
 *  the first 0xaa byte in the DMA buffer (i.e., first 0xaa following sync).
 */

static void *infogrames_longtrack_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset - 15;
        if ((uint16_t)s->word != 0xa144)
            continue;
        if (!check_sequence(s, 6510, 0x00))
            continue;
        if (!check_length(s, 104160))
            break;
        ti->total_bits = 105500;
        return memalloc(0);
    }

    return NULL;
}

static void infogrames_longtrack_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0xa144);
    for (i = 0; i < 6550; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
}

struct track_handler infogrames_longtrack_handler = {
    .write_mfm = infogrames_longtrack_write_mfm,
    .read_mfm = infogrames_longtrack_read_mfm
};

/*
 * TRKTYP_bat_longtrack: B.A.T. by Ubisoft
 *  u16 0x8945
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 109152 bits long (>= 3413 0xa...a longs)
 *  Specifically, protection checks for >= 3412 0xaaaaaaaa mfm longwords
 *  starting 4 bytes into the DMA buffer (i.e., 4 bytes after the sync)
 */

static void *bat_longtrack_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset - 31;
        if ((s->word != 0xaaaa8945) || !check_sequence(s, 6826, 0x00))
            continue;
        if (!check_length(s, 109500))
            break;
        ti->total_bits = 110000;
        return memalloc(0);
    }

    return NULL;
}

static void bat_longtrack_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8945);
    for (i = 0; i < 6840; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
}

struct track_handler bat_longtrack_handler = {
    .write_mfm = bat_longtrack_write_mfm,
    .read_mfm = bat_longtrack_read_mfm
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
