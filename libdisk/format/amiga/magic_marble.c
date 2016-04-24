/*
 * disk/magic_marble.c
 *
 * Custom format as used on Magic Marble by Sphinx.
 *
 * Written in 2015 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 6 possible syncs :: Sync
 *  u32 csum
 *  u32 dat[5968/4] or dat[3032/4]
 *  u32 sum ::sum eor by (((length/4-1)-i) ^ u32) all data
 *
 * TRKTYP_magic_marble data layout:
 *  u8 sector_data[5968]
 *  u8 sector_data[3032]
 */

#include <libdisk/util.h>
#include <private/disk.h>


const static uint32_t syncs[] = {
    0x44894489, 0x22452245, 0x51225122, 0x548a548a, 0x5a495a49, 
	0x12241224
};


static void *magic_marble_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t sync, sum, csum;
    uint32_t raw[2], dat[(ti->len)/4+1];
    unsigned int i, k, counter;
    char *block;

    for (k = 0; k < ARRAY_SIZE(syncs); k++) {

        sync = syncs[k];
        while (stream_next_bit(s) != -1) {

            if (s->word != sync)
                continue;

            ti->data_bitoff = s->index_offset_bc - 31;

            if (stream_next_bytes(s, raw, 8) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            counter = (ti->len)/4;
			for (i = sum = 0; i < ti->len/4; i++) {
                counter--;
				if (stream_next_bytes(s, raw, 8) == -1)
					goto fail;
				mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
				sum += (counter^be32toh(dat[i]));
            }
  
            if (sum != be32toh(csum))
                break;
 
			dat[i] = sync;

            stream_next_index(s);
            ti->total_bits = s->track_len_bc;
            
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

static void magic_marble_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i, counter;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->len/4]);
 
    counter = (ti->len)/4;
    for (i = sum = 0; i < ti->len/4; i++) {
       counter--;
       sum += (counter^be32toh(dat[i]));
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++)
       tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

}

struct track_handler magic_marble_handler = {
    .bytes_per_sector = 5968,
    .nr_sectors = 1,
    .write_raw = magic_marble_write_raw,
    .read_raw = magic_marble_read_raw
};

struct track_handler magic_marble_b_handler = {
    .bytes_per_sector = 3032,
    .nr_sectors = 1,
    .write_raw = magic_marble_write_raw,
    .read_raw = magic_marble_read_raw
};

static void *magic_marble_prot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 31;
        if (s->word != 0xaaaa1224)
            continue;

        ti->total_bits = 96687;
        return memalloc(0);
    }

    return NULL;
}

static void magic_marble_prot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa1224);
    
    for (i = 0; i < 1410; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0xff);
}

struct track_handler magic_marble_prot_handler = {
    .write_raw = magic_marble_prot_write_raw,
    .read_raw = magic_marble_prot_read_raw
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
