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
 *  u16 0x4124,0x4124 (Mickey Mouse 0x4124,0x4324)
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
        if ((s->word != 0x41244124 && s->word != 0x41244324) \
            || !check_sequence(s, 8, 0x00))
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

/* TRKTYP_lankhor1_longtrack: Used on Outzone Lankhor.
 *  u16 0xa144 :: sync
 *  u8[] "PUTE" (encoded bc_mfm)
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104128 bits long (track is ~106000 bits long)
 *  Specifically, protection checks for > 6500 0xaaaa/0x5555 raw words
 *  starting 12 bytes into the DMA buffer (i.e., 12 bytes after the sync) */

/* TRKTYP_lankhor2_longtrack: Used on G.Nius Lankhor.
 *  u16 0xa144 :: sync
 *  u8[] "genius" (encoded bc_mfm)
 *  Rest of track is (MFM-encoded) zeroes
 *  Track is checked to be >= 104128 bits long (track is ~106000 bits long)
 *  Specifically, protection checks for > 6500 0xaaaa/0x5555 raw words
 *  starting 12 bytes into the DMA buffer (i.e., 12 bytes after the sync) */

struct silmarils_info {
    uint16_t type;
    uint32_t sig;
    unsigned int bitlen;
};

const static struct silmarils_info silmarils_infos[] = {
    { TRKTYP_silmarils_longtrack, 0x524f4430, 110000 },  /* "ROD0" */
    { TRKTYP_lankhor1_longtrack, 0x50555445, 106000 },   /* "PUTE" */
    { TRKTYP_lankhor2_longtrack, 0x67656e69, 106000 }    /* "geni" */
};

static const struct silmarils_info *find_silmarils_info(uint16_t type)
{
    const struct silmarils_info *silmarils_info;
    for (silmarils_info = silmarils_infos; silmarils_info->type != type; silmarils_info++)
        continue;
    return silmarils_info;
}

static void *silmarils_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t raw[2];
    uint16_t raw16[2];
    const struct silmarils_info *silmarils_info = find_silmarils_info(ti->type);

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 15;
        if (s->word != 0xaaaaa144)
            continue;
        stream_next_bytes(s, raw, 8);
        mfm_decode_bytes(bc_mfm, 4, raw, raw);
        if (be32toh(raw[0]) != silmarils_info->sig)
            continue;
        if (silmarils_info->type == TRKTYP_lankhor2_longtrack){
            stream_next_bytes(s, raw16, 4);
            mfm_decode_bytes(bc_mfm, 2, raw16, raw16);
            if(be16toh(raw16[0]) != 0x7573) /* "us" */
                continue;
        }
        if (!check_sequence(s, 6500, 0x00))
            continue;
        if (!check_length(s, 104128))
            break;

        ti->total_bits = silmarils_info->bitlen;
        return memalloc(0);
    }

    return NULL;
}

static void silmarils_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;
    const struct silmarils_info *silmarils_info = find_silmarils_info(ti->type);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa144);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, silmarils_info->sig);
    for (i = 0; i < 6550; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
}

struct track_handler silmarils_longtrack_handler = {
    .write_raw = silmarils_longtrack_write_raw,
    .read_raw = silmarils_longtrack_read_raw
};

struct track_handler lankhor1_longtrack_handler = {
    .write_raw = silmarils_longtrack_write_raw,
    .read_raw = silmarils_longtrack_read_raw
};

struct track_handler lankhor2_longtrack_handler = {
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
 * 
 * Capone
 * GCR fffff....
 * Long track (100300/2 GCR bits).
 */

struct gcr_protection_info {
    uint32_t pattern;
    unsigned int bitlen;
};

static void *gcr_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct gcr_protection_info *info = handlers[ti->type]->extra_data;
    uint32_t prev_offset;
    unsigned int match = 0;

    /* GCR 4us bit time */
    stream_set_density(s, 4000);

    do {
        prev_offset = s->index_offset_bc;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        while (s->word != info->pattern) {
            if (stream_next_bit(s) == -1)
                goto fail;
            if (s->index_offset_bc <= prev_offset)
                break;
        }
        match++;
    } while (s->index_offset_bc > prev_offset);

    /* We want to see predominantly GCR info->pattern. */
    if (match < (100000/(2*32)))
        return NULL;

    /* We will generate a gap-less track, so make it a 32-bitcell multiple 
     * starting exactly on the index. */
    ti->total_bits = (info->bitlen/2) & ~31;
    ti->data_bitoff = 0;
    return memalloc(0);

fail:
    return NULL;
}

static void gcr_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct gcr_protection_info *info = handlers[ti->type]->extra_data;
    int nr = ti->total_bits / 32;
    while (nr--)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->pattern);
}

struct track_handler supermethanebros_longtrack_handler = {
    .write_raw = gcr_protection_write_raw,
    .read_raw = gcr_protection_read_raw,
    .extra_data = & (struct gcr_protection_info) {
        .pattern = 0x99999999,
        .bitlen = 105500
    }
};

struct track_handler actionware_protection_handler = {
    .write_raw = gcr_protection_write_raw,
    .read_raw = gcr_protection_read_raw,
    .extra_data = & (struct gcr_protection_info) {
        .pattern = 0xffffffff,
        .bitlen = 100300
    }
};

/*
 * Alternate Reality GCR Protection
 * Long track (116778/2 GCR bits) but this isn't properly checked.
 * 
 * The protection checks for the pattern 0xcc96aa within the first 0x300
 * bytes and if it finds it, it adds the offset of 0x1560 + offest of first 
 * instance from the start of the raw data and checks for the same pattern.
 * It then checks the next six bytes from the first instance against the
 * next 6 bytes of the second instance and verifies they are the same.
 * 
 * The data between the gap is not checked and is was different in the 2
 * dumps I tested against.
 * 
 * Filling the track with 0xffcc96aa passes the protection check.
 */

static void *alternate_reality_gcr_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct gcr_protection_info *info = handlers[ti->type]->extra_data;

    /* GCR 4us bit time */
    stream_set_density(s, 4000);

    while (stream_next_bit(s) != -1) {
        if (s->word == info->pattern)
            break;
    }

    if (s->word != info->pattern)
        goto fail;

    /* We will generate a gap-less track, so make it a 32-bitcell multiple 
     * starting exactly on the index. */
    ti->total_bits = (info->bitlen/2) & ~31;
    ti->data_bitoff = 0;
    return memalloc(0);

fail:
    return NULL;
}

struct track_handler alternate_reality_gcr_protection_handler = {
    .write_raw = alternate_reality_gcr_protection_write_raw,
    .read_raw = gcr_protection_read_raw,
    .extra_data = & (struct gcr_protection_info) {
        .pattern = 0xffcc96aa,
        .bitlen = 116778
    }
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

/* TRKTYP_zoom_longtrack:
 *  This protections is used by Zoom!, Grid Start and Cyber World
 *  Check for 0x31f8 bytes of either 0x11, 0x22, 0x44, or 0x88 with a single
 *  byte that is not 0x11, 0x22, 0x44, or 0x88
 *  example: 0x22 0x22.....0x22 0xaa 0x22
 */

static void *zoom_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        ti->data_bitoff = s->index_offset_bc - 15;
        if (!check_sequence(s, 3000, 0xaa))
            continue;
        if (!check_length(s, 101200))
            break;

        ti->total_bits = 102400;
        return memalloc(0);
    }

    return NULL;
}

static void zoom_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    for (i = 0; i < 6200*2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0x22);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0xaa);
    for (i = 0; i < 200*2-1; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0x22);
}

struct track_handler zoom_longtrack_handler = {
    .write_raw = zoom_longtrack_write_raw,
    .read_raw = zoom_longtrack_read_raw
};

/* TRKTYP_gauntlet2_longtrack:
 *  Essentially measures distance between 44894489 syncwords.
 *  Relies on track 79.0 being standard length and 79.1 being long.
 *  It doesn't actually seem to care *how* much longer 79.1 is.
 */

static void *gauntlet2_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        if (s->word == 0x44894489)
            goto found;
    }
    return NULL;

found:
    ti->data_bitoff = 200;
    ti->total_bits = (tracknr == 158) ? 102000 : 105500;
    return memalloc(0);
}

static void gauntlet2_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
}

struct track_handler gauntlet2_longtrack_handler = {
    .write_raw = gauntlet2_longtrack_write_raw,
    .read_raw = gauntlet2_longtrack_read_raw
};

/* TRKTYP_demonware_protection:
 *  Looks for 1023 consecutive 0x4552 words right after the sync.  This is used
 *  by the game Ooops Up and The Power
 */

static void *demonware_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        if ((uint16_t)s->word != 0x4492)
            continue;

        if (!check_sequence(s, 1020, 0xbc))
            continue;

        if (!check_length(s, 99800))
            break;

        ti->data_bitoff = 0;
        return memalloc(0);
    }
    return NULL;
}

static void demonware_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4492);
    for (i = 0; i < 1200; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4552);
}

struct track_handler demonware_protection_handler = {
    .write_raw = demonware_protection_write_raw,
    .read_raw = demonware_protection_read_raw
};

/* TRKTYP_cyberdos_protection:
 * The contents of the track are not checked, just the length of the
 * track is checked
 * 
 * Tested with version 3.84 using the IPF
 * Tested with version 4.01 with IPF and Fist of Fury edition from BarryB
 * Version 4.16 does does not have a protection track and it is unformatted
 * 
 * Could have used Empty Longtrack instead, but wanted to keep the data and length
 * like the original
 */

static void *cyberdos_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
    
        if (!check_length(s, 111000))
            break;

        ti->data_bitoff = 0;
        ti->total_bits = 111320;
        return memalloc(0);
    }
    return NULL;
}

static void cyberdos_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    for (i = 0; i < 6900; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9494);
}

struct track_handler cyberdos_protection_handler = {
    .write_raw = cyberdos_protection_write_raw,
    .read_raw = cyberdos_protection_read_raw
};

/* TRKTYP_bomb_busters_longtrack:
 *
 *  This protections is used by Bomb Busters by Readysoft!
 *  Check for 0xffe consecutive words.  It first reads the first word from the track
 *  then compares the next 0xffe words with this value. The protection will fail if
 *  it finds a different value.
 */

static void *bomb_busters_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (!check_sequence(s, 3000, 0x55))
            continue;

        if (!check_length(s, 101200))
            break;

        ti->data_bitoff = 0;
        ti->total_bits = 102400;
        return memalloc(0);
    }

    return NULL;
}

static void bomb_busters_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    for (i = 0; i < 6400*2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0x11);
}

struct track_handler bomb_busters_longtrack_handler = {
    .write_raw = bomb_busters_longtrack_write_raw,
    .read_raw = bomb_busters_longtrack_read_raw
};

/* TRKTYP_the_oath: Protection used on The Oath by attic Entertainment.
 * Normal length track 81.0, full of rubbish. Has a (poor) sync word 0x2195
 * and expects to find 0x4489 at a certain offset later.
 */

static void *the_oath_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    int i;

    while (stream_next_bit(s) != -1) {

        /* Allow 0x2155 as a common corruption of 0x2195. */
        if (((uint16_t)s->word != 0x2195) && ((uint16_t)s->word != 0x2155))
            continue;

        /* Allow some slack in looking for 4489 match, as original track
         * matches on more than one 2195 sync and may thus "slip" some bits as 
         * it WORDSYNCs each time. */
        stream_next_bits(s, 0x3008*8);
        for (i = 0; i < 32; i++) {
            if ((uint16_t)s->word == 0x4489)
                goto found;
            stream_next_bit(s);
        }
    }

    return NULL;

found:
    ti->data_bitoff = 1024;
    ti->total_bits = 101500;
    return memalloc(0);
}

static void the_oath_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    /* Repeat the sync a few times to improve chances of a good read. */
    for (i = 0; i < 2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x21952195);
    /* Garbage in original track replaced with emptiness. */
    for (i = 0; i < 0x1800; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    /* Make a larger 4489 sync "landing strip". */
    for (i = 0; i < 8; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
}

struct track_handler the_oath_handler = {
    .write_raw = the_oath_write_raw,
    .read_raw = the_oath_read_raw
};

/* TRKTYP_golden_path_longtrack:
 *
 *  This protection is used by Golden Path from Firebird Software.
 *  Locates the first instance of 0x4454 and then calculates the length of
 *  the gap to the next instance of 0x4454. The gap must be larger than
 *  0x1a2c. The Computer Hits Volume 2 version has the protection check
 *  code in place, but has been modified to always pass the protection 
 *  check.
 */

static void *golden_path_longtrack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if (!check_sequence(s, 16, 0x66))
            continue;

        if (!check_length(s, 110000))
            break;

        ti->data_bitoff = 0;
        ti->total_bits = 111632;
        return memalloc(0);
    }

    return NULL;
}

static void golden_path_longtrack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;
    for (i = 0; i < 0x20; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9494);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4454);
    for (i = 0; i < 0x1b10; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9494);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44544454);
}

struct track_handler golden_path_longtrack_handler = {
    .write_raw = golden_path_longtrack_write_raw,
    .read_raw = golden_path_longtrack_read_raw
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
