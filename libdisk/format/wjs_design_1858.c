/*
 * disk/wjs_design_1858.c
 *
 * Custom format as used on Beastlord, Creatures, Ork, and Spell Bound
 * by Psyclapse/Psygnosis.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x2924,0x9492,0x4a45,0x2511 :: Beastlord Disk 1 Sync
 *  u16 0x4489,0x2924,0x9491,0x4a45,0x2512 :: Beastlord Disk 2 Sync
 *  u16 0x4489,0x2929,0x2a92,0x4952,0x5491 :: Creatures Disk 1 Sync
 *  u16 0x4489,0x2929,0x2a91,0x4952,0x5492 :: Creatures Disk 2 Sync
 *  u16 0x4489,0x2529,0x2512,0x4552,0x4911 :: Ork Disk 1 Sync
 *  u16 0x4489,0x2529,0x2511,0x4552,0x4912 :: Ork Disk 2 Sync
 *  u16 0x4489,0x2924,0xA92A,0x4449,0x5245 :: Spell Bound Sync
 *  u32 checksum
 *  u32 dat[6232/4]
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[6232]
 */

#include <libdisk/util.h>
#include "../private.h"

static void *wjs_design_1858_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x616], csum, sum, total_bits;;
        unsigned int i;
        char *block;

		total_bits = 105800;

        if ((uint16_t)s->word != 0x4489)
            continue;

        if (ti->type == TRKTYP_ork_a) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x25292512)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x45524911)
				continue;
        } else if (ti->type == TRKTYP_ork_b) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x25292511)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x45524912)
				continue;
        } else if (ti->type == TRKTYP_beastlord_a) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x29249492)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x4a452511)
				continue;
			total_bits = 103000;
        } else if (ti->type == TRKTYP_beastlord_b) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x29249491)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x4a452512)
				continue;
			total_bits = 103000;
        } else if (ti->type == TRKTYP_creatures_a) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x29292a92)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x49525491)
				continue;
        } else if (ti->type == TRKTYP_creatures_b) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x29292a91)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x49525492)
				continue;
        } else if (ti->type == TRKTYP_spell_bound) {
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x2924A92A)
				continue;
			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0x44495245)
				continue;
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sum);
        sum = be32toh(sum);

        ti->data_bitoff = s->index_offset - 46;

        for (i = csum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum ^= be32toh(dat[i]);
        }

       if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = total_bits;
        return block;
    }

fail:
    return NULL;
}



static void wjs_design_1858_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

	tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    if (ti->type == TRKTYP_ork_a) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x25292512);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x45524911);
	} else if (ti->type == TRKTYP_ork_b) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x25292511);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x45524912);
	} else if (ti->type == TRKTYP_beastlord_a) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x29249492);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4a452511);
	} else if (ti->type == TRKTYP_beastlord_b) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x29249491);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4a452512);
	} else if (ti->type == TRKTYP_creatures_a) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x29292a92);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x49525491);
	} else if (ti->type == TRKTYP_creatures_b) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x29292a91);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x49525492);
	} else if (ti->type == TRKTYP_spell_bound) {
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2924A92A);
		tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44495245);
	}

    for (i = csum = 0; i < ti->len/4; i++)
        csum ^= be32toh(dat[i]);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,csum);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

}

struct track_handler ork_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler ork_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler beastlord_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler beastlord_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler creatures_a_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler creatures_b_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
};

struct track_handler spell_bound_handler = {
    .bytes_per_sector = 6232,
    .nr_sectors = 1,
    .write_raw = wjs_design_1858_write_raw,
    .read_raw = wjs_design_1858_read_raw
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