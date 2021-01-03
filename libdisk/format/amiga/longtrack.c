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
#include <private/disk.h>

static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

static int check_length(struct stream *s, unsigned int min_bits)
{
    stream_next_index(s);
    return (s->track_len_bc >= min_bits);
}

/* TRKTYP_protec_longtrack: PROTEC protection track, used on many releases
 *  u16 0x4454
 *  u8 0x33 (encoded in-place, 1000+ times, to track gap)
 *  Track is checked to be >= 107200 bits long
 *  Specifically, protection checks for >= 6700 raw words between successive
 *  sync marks. Track contents are not otherwise checked or tested. 
 * NOTES: 
 *  1. Repeated pattern byte can differ (e.g. SPS 1352, Robocod, uses pattern
 *     byte 0x44). We simply check for any repeated value, and use that same
 *     value when regenerating the MFM data. */

static void *protec_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t byte, *data;

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 31;
        if ((s->word >> 16) != 0x4454)
            continue;
        byte = (uint8_t)mfm_decode_word(s->word);
        if (!check_sequence(s, 1000, byte))
            continue;
        if (!check_length(s, 107200))
            break;
        printf("Len: %u\n", s->track_len_bc);
        ti->total_bits = 110000; /* long enough */
        ti->len = 1;
        data = memalloc(ti->len);
        *data = byte;
        return data;
    }

    return NULL;
}

static void protec_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat, byte = *dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4454);
    for (i = 0; i < 6000; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, byte);
}

struct track_handler protec_longtrack_handler = {
    .write_raw = protec_longtrack_write_raw,
    .read_raw = protec_longtrack_read_raw
};

/* TRKTYP_protoscan_longtrack: Lotus I/II, + many others
 *  u16 0x4124,0x4124
 *  Rest of track is (MFM-encoded) zeroes, and/or unformatted garbage.
 *  The contents are never checked, only successive sync marks are scanned for.
 * 
 *  Track is checked to be >= 102400 bits long.
 *  Specifically, protection checks for >= 6400 raw words between
 *  successive sync marks. Track contents are not otherwise checked or tested.
 * 
 *  Track is typically ~105500 bits long. */

static void *protoscan_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 31;
        if ((s->word != 0x41244124) || !check_sequence(s, 8, 0x00))
            continue;
        if (ti->type != TRKTYP_tiertex_longtrack)
            ti->total_bits = 105500;
        return memalloc(0);
    }

    return NULL;
}

static void protoscan_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x41244124);
    for (i = 0; i < (ti->total_bits/16)-250; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
}

struct track_handler protoscan_longtrack_handler = {
    .write_raw = protoscan_longtrack_write_raw,
    .read_raw = protoscan_longtrack_read_raw
};

/* TRKTYP_tiertex_longtrack: Strider II
 *  A variant of the Protoscan long track, checks 99328 <= x <= 103680 bits.
 *  Specifically, the variant checks 6208 <= x <= 6480 raw words between
 *  successive sync marks. Track contents are not otherwise checked or tested.
 * 
 *  Track is actually ~100150 bits long (normal length!). */

struct track_handler tiertex_longtrack_handler = {
    .write_raw = protoscan_longtrack_write_raw,
    .read_raw = protoscan_longtrack_read_raw
};

/* TRKTYP_silmarils_longtrack: Used on French titles by Silmarils and Lankhor.
 *  u16 0xa144 :: sync
 *  u8[] "ROD0" (encoded bc_mfm)
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104128 bits long (track is ~110000 bits long)
 *  Specifically, protection checks for > 6500 0xaaaa/0x5555 raw words
 *  starting 12 bytes into the DMA buffer (i.e., 12 bytes after the sync) */

static void *silmarils_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t raw[2];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 15;
        if (s->word != 0xaaaaa144)
            continue;
        stream_next_bytes(s, raw, 8);
        mfm_decode_bytes(bc_mfm, 4, raw, raw);
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

static void silmarils_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0x524f4430); /* "ROD0" */
    for (i = 0; i < 6550; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
}

struct track_handler silmarils_longtrack_handler = {
    .write_raw = silmarils_longtrack_write_raw,
    .read_raw = silmarils_longtrack_read_raw
};

/* TRKTYP_infogrames_longtrack: Hostages, Jumping Jack Son, and others
 *  u16 0xa144 :: sync
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104160 bits long (track is ~105500 bits long)
 *  Specifically, protection checks for > 13020 0xaa raw bytes, starting from
 *  the first 0xaa byte in the DMA buffer (i.e., first 0xaa following sync). */

static void *infogrames_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 15;
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

static void infogrames_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    for (i = 0; i < 6550; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
}

struct track_handler infogrames_longtrack_handler = {
    .write_raw = infogrames_longtrack_write_raw,
    .read_raw = infogrames_longtrack_read_raw
};

/* TRKTYP_prolance_longtrack: PROTEC variant used on B.A.T. by Ubisoft
 *  u16 0x8945
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 109152 bits long (>= 3413 0xa...a longs)
 *  Specifically, protection checks for >= 3412 0xaaaaaaaa raw longwords
 *  starting 4 bytes into the DMA buffer (i.e., 4 bytes after the sync) */

static void *prolance_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 31;
        if ((s->word != 0xaaaa8945) || !check_sequence(s, 6826, 0x00))
            continue;
        if (!check_length(s, 109500))
            break;
        ti->total_bits = 110000;
        return memalloc(0);
    }

    return NULL;
}

static void prolance_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8945);
    for (i = 0; i < 6840; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
}

struct track_handler prolance_longtrack_handler = {
    .write_raw = prolance_longtrack_write_raw,
    .read_raw = prolance_longtrack_read_raw
};

/* TRKTYP_app_longtrack: Amiga Power Pack by Softgang
 *  u16 0x924a :: MFM sync
 *  u8 0xdc (6600 times, = 105600 MFM bits)
 *  Track gap is zeroes. Track total length is ~111000 bits. */

static void *app_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 15;
        if (((uint16_t)s->word != 0x924a) || !check_sequence(s, 6600, 0xdc))
            continue;
        if (!check_length(s, 110000))
            break;
        ti->total_bits = 111000;
        return memalloc(0);
    }

    return NULL;
}

static void app_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x924a);
    for (i = 0; i < 6600; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xdc);
}

struct track_handler app_longtrack_handler = {
    .write_raw = app_longtrack_write_raw,
    .read_raw = app_longtrack_read_raw
};

/* TRKTYP_sevencities_longtrack: Seven Cities Of Gold by Electronic Arts
 * Not really a long track.
 *  9251 sync; 122 bytes MFM data; MFM-encoded zeroes...; 924a sync.
 * MFM data string is combined with gap between sync words to compute a key. */
#define SEVENCITIES_DATSZ 122
static void *sevencities_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = memalloc(SEVENCITIES_DATSZ);
    unsigned int i;

    /* Check for 924a sync word */
    while (stream_next_bit(s) != -1)
        if ((uint16_t)s->word == 0x924a)
            break;

    while (stream_next_bit(s) != -1) {
        /* Check for 9251 sync word */
        if ((uint16_t)s->word != 0x9251)
            continue;
        /* Next 122 bytes are used by protection check. They have a known 
         * CRC which we check here, and save the bytes as track data. */
        stream_start_crc(s);
        for (i = 0; i < SEVENCITIES_DATSZ; i++) {
            stream_next_bits(s, 8);
            dat[i] = (uint8_t)s->word;
        }
        if (s->crc16_ccitt != 0x010a)
            continue;
        /* Done. */
        ti->len = SEVENCITIES_DATSZ;
        ti->data_bitoff = 76000;
        ti->total_bits = 101500;
        return dat;
    }

    memfree(dat);
    return NULL;
}

static void sevencities_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9251);
    for (i = 0; i < ti->len; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, dat[i]);
    for (i = 0; i < 6052-(ti->len/2); i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x0480);
}

struct track_handler sevencities_longtrack_handler = {
    .write_raw = sevencities_longtrack_write_raw,
    .read_raw = sevencities_longtrack_read_raw
};

/*
 * Super Methane Bros.
 * GCR 99999....
 * Long track (105500/2 GCR bits) but this isn't properly checked.
 */

static void *supermethanebros_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t prev_offset;
    unsigned int match = 0;

    /* GCR 4us bit time */
    stream_set_density(s, 4000);

    do {
        prev_offset = s->index_offset_bc;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        while (s->word != 0x99999999) {
            if (stream_next_bit(s) == -1)
                goto fail;
            if (s->index_offset_bc <= prev_offset)
                break;
        }
        match++;
    } while (s->index_offset_bc > prev_offset);

    /* We want to see predominantly GCR 99999999. */
    if (match < (100000/(2*32)))
        return NULL;

    /* We will generate a gap-less track, so make it a 32-bitcell multiple 
     * starting exactly on the index. */
    ti->total_bits = (105500/2) & ~31;
    ti->data_bitoff = 0;
    return memalloc(0);

fail:
    return NULL;
}

static void supermethanebros_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    int nr = ti->total_bits / 32;
    while (nr--)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x99999999);
}

struct track_handler supermethanebros_longtrack_handler = {
    .write_raw = supermethanebros_longtrack_write_raw,
    .read_raw = supermethanebros_longtrack_read_raw,
};

/*
 * All MFM zeroes.
 */

static void *zeroes_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t prev_offset, prev_word;
    unsigned int discontinuities, run, max_run;

    stream_next_bits(s, 32);
    run = ((s->word == 0xaaaaaaaa) || (s->word == 0x55555555));
    max_run = discontinuities = 0;

    do {
        prev_word = s->word;
        prev_offset = s->index_offset_bc;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (run && (s->word == prev_word)) {
            run++;
        } else {
            discontinuities++;
            max_run = max(max_run, run);
            run = ((s->word == 0xaaaaaaaa) || (s->word == 0x55555555));
        }
    } while (s->index_offset_bc > prev_offset);

    /* Not too many discontinuities and a nice long run of zeroes. */
    max_run = max(max_run, run);
    if ((discontinuities > 5) || (max_run < (99000/32)))
        return NULL;

    ti->data_bitoff = ti->total_bits / 2; /* write splice at index */
    return memalloc(0);

fail:
    return NULL;
}

static void zeroes_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    /* Emit some data: prevents IPF handler from barfing on no data blocks. */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
}

struct track_handler zeroes_handler = {
    .write_raw = zeroes_write_raw,
    .read_raw = zeroes_read_raw,
};

/*
 * Empty track seen on Zero Issue 18 April 1991 Dual-Format Cover Disk.
 */

static void rnc_dualformat_empty_read_sectors(
    struct disk *d, unsigned int tracknr, struct track_sectors *sectors)
{
    sectors->nr_bytes = 10*512;
    sectors->data = memalloc(sectors->nr_bytes);
}

struct track_handler rnc_dualformat_empty_handler = {
    .write_raw = zeroes_write_raw,
    .read_raw = zeroes_read_raw,
    .read_sectors = rnc_dualformat_empty_read_sectors
};

/* TRKTYP_empty_longtrack:
 *  Entire track is (MFM-encoded) zeroes
 *  Track is only checked to be of a certain length. */

static void *empty_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    if (!check_length(s, 105000))
        return NULL;

    ti->total_bits = 110000;
    ti->data_bitoff = ti->total_bits / 2; /* write splice at index */
    return memalloc(0);
}

static void empty_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    /* Emit some data: prevents IPF handler from barfing on no data blocks. */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
}

struct track_handler empty_longtrack_handler = {
    .write_raw = empty_longtrack_write_raw,
    .read_raw = empty_longtrack_read_raw
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
