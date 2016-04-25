/*
 * disk/konami.c
 *
 * Custom format as used on Back to the Future III and
 * Teenage Mutant Ninja Turtles - The Arcade Game from
 * Konami/Mirrorsoft.
 *
 * Written in 2014/2016 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 0x552524a4
 *  u32 0x554a4945
 *  u32 dat[6144/4]
 *  u32 checksum
 *
 * The checksum for most tracks is calculated with a length of
 * 6144.  However, the length is not always 6144.  Rather than
 * creating a handler for each possible length I create an array
 * of track sizes and loop through the sizes until the checksum
 * match or the end of the array is reached.
 *
 * One version of Back to the Future III uses manual protection
 * rather than using a copylock track.
 *
 *
 * TRKTYP_back_future3 data layout:
 *  u8 sector_data[6144]
 */



#include <libdisk/util.h>
#include <private/disk.h>

const static uint16_t track_sizes[] = {
    6144, 5632, 5120, 4608, 4096, 3584,
    3072, 2560, 2560, 2048, 1536, 1024,
    512
};

static void *konami_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(track_sizes); k++) {

        ti->len = track_sizes[k];

        while (stream_next_bit(s) != -1) {

            uint32_t raw[2], dat[ti->len/4], sum, csum;
            unsigned int i;
            char *block;

            if ((uint16_t)s->word != 0x4489)
                continue;

            ti->data_bitoff = s->index_offset_bc - 15;

            if (stream_next_bits(s, 32) == -1)
                break;
            if (s->word != 0x552524a4)
                continue;
            if (stream_next_bits(s, 32) == -1)
                break;
            if (s->word != 0x554a4945)
                continue;

            for (i = sum = 0; i < ti->len/4; i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    break;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
                sum += be32toh(dat[i]);
            }

            if (stream_next_bytes(s, raw, 8) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            if (sum != be32toh(csum))
                break;

            block = memalloc(ti->len);
            memcpy(block, dat, ti->len);
            set_all_sectors_valid(ti);
            return block;
        }
        stream_reset(s);
    }

    return NULL;
}



static void konami_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x552524a4);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x554a4945);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
}

struct track_handler konami_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = konami_write_raw,
    .read_raw = konami_read_raw
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
