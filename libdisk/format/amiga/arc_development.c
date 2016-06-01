/*
 * disk/arc_development.c
 *
 * Custom format as used by Arc Development for the following games:
 *  Beavers
 *  McDonald Land
 *  The Simpsons, Bart vs the World
 *  Armalyte
 *  The Simpsons, Bart vs the Space Mutants
 *  Dragon Breed
 *  Predator II
 *  Nick Faldo's Championship Golf
 *  Robozone
 *  R-Type II
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489
 *  u16 0x4489 :: Extra sync in Format B
 *  u16 0
 *  u32 data[12][512]
 *  u32 checksum
 *
 * TRKTYP_arc_development data layout:
 *  u8 sector_data[12][512]
 *
 * Forgotten Worlds
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 ::  Sync
 *  u16 track number
 *  u16 dat[6144/2]
 *  u16 checksum - eor.w over data
 *
 * TRKTYP_hi_forgotten_worlds data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *arc_development_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[12*512/4];
        uint16_t  csum, sum, craw[2];
        unsigned int i, base;
        unsigned int sec;
        char *block;

        /* Both formats have at least one sync word. */
        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (s->word == 0x44894489) {
            /* Two sync words is format B. */
            ti->type = TRKTYP_arc_development_b;
            ti->data_bitoff = s->index_offset_bc - 31;
        } else if (ti->type == TRKTYP_arc_development_b) {
            /* Format B must have two sync words. */
            continue;
        }

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (mfm_decode_word(s->word) != 0)
           continue;

        for (sec = base = 0; sec < ti->nr_sectors; sec++) {
            for (i = sum = 0; i < 512/4; i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    goto fail;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i+base]);
                sum ^= (uint16_t)be32toh(dat[i+base]);
            }
            base += 512/4;

            if (stream_next_bytes(s, craw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &csum);

            if (sum != be16toh(csum))
                goto fail;
        }

        /* Some releases use long tracks (for no good reason). */
        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 107000) ? 111000
            : (s->track_len_bc > 102000) ? 105500
            : 100000;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void arc_development_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i, sec, base;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    if (ti->type == TRKTYP_arc_development_b)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);

    for (sec = base = 0; sec < ti->nr_sectors; sec++) {
        for (i = csum = 0; i < 512/4; i++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                      be32toh(dat[i+base]));
            csum ^= (uint16_t)be32toh(dat[i+base]);
        }
        base += 512/4;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
    }
}

struct track_handler arc_development_a_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = arc_development_write_raw,
    .read_raw = arc_development_read_raw
};

struct track_handler arc_development_b_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = arc_development_write_raw,
    .read_raw = arc_development_read_raw
};

static void *forgotten_worlds_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[ti->len/2], trk, sum, csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &trk);
        if(tracknr != be16toh(trk))
            continue;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum ^= be16toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 4) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &csum);

        if(sum != be16toh(csum))
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

static void forgotten_worlds_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat, csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)tracknr);

    for (i = csum =0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        csum ^= be16toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
}

struct track_handler forgotten_worlds_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = forgotten_worlds_write_raw,
    .read_raw = forgotten_worlds_read_raw
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
