/*
 * disk/spherical.c
 *
 * Custom format as used on Spherical & Conqueror by Rainbow Arts.
 *
 * Written in 2012 by Keir Fraser
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2aaa :: Sync for Spherical, Conqueror
 *  u16 0x4445,0x2aaa :: Sync for Conqueror
 *  u32 dat[0x500][2] :: Interleaved even/odd
 *  u32 csum[2] :: Even/odd, ADD.L sum over data
 *
 * TRKTYP_rainbow_arts data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rainbow_arts_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x501], csum, sync;
        unsigned int i;
        char *block;

        sync = (ti->type == TRKTYP_spherical) ? 0x44892aaa
            : 0x44452aaa;

        if (s->word != sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum += be32toh(dat[i]);
        }

        csum -= 2*be32toh(dat[i-1]);
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101200;
        return block;
    }

fail:
    return NULL;
}

static void rainbow_arts_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    uint16_t sync;
    unsigned int i;

    sync = (ti->type == TRKTYP_spherical) ? 0x4489
        : 0x4445;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler spherical_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
};

struct track_handler conqueror_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbow_arts_write_raw,
    .read_raw = rainbow_arts_read_raw
};


/*
 * Custom protection track format as used on following games:
 *
 * - Jinks                              Diamond Software / Rainbow Arts
 * - Street Cat                         Rainbow Arts
 * - Bad Cat                            Rainbow Arts
 * - In Eighty Days Around The World    Rainbow Arts
 * - Mission Elevator                   Reline/Rainbow Arts
 * - Crystal Hammer                     Reline
 * - Spaceport                          Reline
 * 
 * Written in 2014 by Keir Fraser
 * Updated in 2022 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 * 
 * rainbow_arts_protection_a
 *  u32 0x92429242
 *  u32 0xaa1191aa (track 158)
 * 
 * rainbow_arts_protection_b
 *  u32 0x44894489
 *  u32 0x554A52AA (track 158)
 * 
 * rainbow_arts_protection_c
 *  u32 0x92459245
 *  u32 0xAA9494AA (track 158)
 * 
 * rainbow_arts_protection_d
 *  u32 0x92454922
 *  u32 0xAA9494AA (track 158)
 * 
 * Normal length track.
 * 
 * Protection reads the longword following sync from track 158. Converts this
 * to an offset X. Then syncs to track 159, then steps immediately to track
 * 161 and does an unsynced read of 512 words. Then expects to find sync 
 * at around offset X in the MFM buffer.
 * 
 * This is obviously quite imprecise, so we make the check a dead certainty
 * by stamping sync throughout track 161. We adjust this track's start
 * point to provide a large landing area for the protection check.
 */

struct rainbow_arts_info {
    uint32_t sync;
    uint32_t encoded_offset;
};


static void *rainbow_arts_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct rainbow_arts_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        if (s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (tracknr == 161)
            ti->data_bitoff -= 1000;

        return memalloc(0);
    }

    return NULL;
}

static void rainbow_arts_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int nr = (tracknr == 161) ? 3000 : 1;
    struct track_info *ti = &d->di->track[tracknr];
    const struct rainbow_arts_info *info = handlers[ti->type]->extra_data;

    while (nr--)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sync);
    if (tracknr == 158)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->encoded_offset);
}

/*
  Jinks
*/
struct track_handler rainbow_arts_protection_a_handler = {
    .write_raw = rainbow_arts_protection_write_raw,
    .read_raw = rainbow_arts_protection_read_raw,
    .extra_data = & (struct rainbow_arts_info) {
        .sync = 0x92429242,
        .encoded_offset = 0xaa1191aa
    }
};

/*
  Mission Elevator
  Crystal Hammer
  Spaceport
  Clever & Smart
*/
struct track_handler rainbow_arts_protection_b_handler = {
    .write_raw = rainbow_arts_protection_write_raw,
    .read_raw = rainbow_arts_protection_read_raw,
    .extra_data = & (struct rainbow_arts_info) {
        .sync = 0x44894489,
        .encoded_offset = 0x554A52AA
    }
};

/*
  Street Cat
  Bad Cat
  In 80 Days Around the Wolrd
*/

struct track_handler rainbow_arts_protection_c_handler = {
    .write_raw = rainbow_arts_protection_write_raw,
    .read_raw = rainbow_arts_protection_read_raw,
    .extra_data = & (struct rainbow_arts_info) {
        .sync = 0x92459245,
        .encoded_offset = 0xAA9494AA
    }
};

/*
  Street Cat
*/
struct track_handler rainbow_arts_protection_d_handler = {
    .write_raw = rainbow_arts_protection_write_raw,
    .read_raw = rainbow_arts_protection_read_raw,
    .extra_data = & (struct rainbow_arts_info) {
        .sync = 0x92454922,
        .encoded_offset = 0xAA9494AA
    }
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
