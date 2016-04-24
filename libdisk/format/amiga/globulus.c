/*
 * disk/globulus.c
 *
 * Custom format as used on Globulus by Innerprise.
 *
 * Written in 2016 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 10 possible syncs :: Sync
 *  u32 0xaaaaaaaa
 *  u32 dat[5636/4]
 *  u32 sum ::add.l over all data
 *
 * TRKTYP_globulus data layout:
 *  u8 sector_data[5636]
 */

#include <libdisk/util.h>
#include <private/disk.h>


const static uint32_t syncs[] = {
    0x44894489, 0x448A448A, 0x89448944, 0x8A448A44, 0x12251225, 
	0xA244A244, 0x44A244A2, 0x22442244, 0x12291229, 0x8A448A44 
};

static void *globulus_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t sync, sum;
    uint32_t raw[2], dat[(ti->len)/4+1];
    unsigned int i, k;
    char *block;

    for (k = 0; k < ARRAY_SIZE(syncs); k++) {

        sync = syncs[k];
        while (stream_next_bit(s) != -1) {

            if (s->word != sync)
                continue;
 
            ti->data_bitoff = s->index_offset_bc - 31;

			if (stream_next_bits(s, 32) == -1)
				goto fail;
			if (s->word != 0xaaaaaaaa)
				continue;

			for (i = sum = 0; i < ti->len/4; i++) {
				if (stream_next_bytes(s, raw, 8) == -1)
					goto fail;
				mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
				if(i < (ti->len/4)-1)
					sum += be32toh(dat[i]);
			}

            if (sum != be32toh(dat[i-1]))
                continue;

			dat[i] = sync;

            stream_next_index(s);
            ti->total_bits = (s->track_len_bc > 104000)
                ? 104300 : 101500;

            block = memalloc(ti->len+4);
            memcpy(block, dat, ti->len+4);
            set_all_sectors_valid(ti);
            return block;
        }
        stream_reset(s);
    }

fail:
    return NULL;
}

static void globulus_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->len/4]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32,0xaaaaaaaa);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler globulus_handler = {
    .bytes_per_sector = 5636,
    .nr_sectors = 1,
    .write_raw = globulus_write_raw,
    .read_raw = globulus_read_raw
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
