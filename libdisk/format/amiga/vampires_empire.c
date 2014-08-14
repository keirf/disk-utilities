/*
 * disk/vampires_empire.c
 *
 * Custom format as used on Vampires Empire by DigiTek.
 *
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync 
 *  u16 0x4489, 0x4489  :: Sync tracks 46 & 51
 *  u32 dat[0xc00][2] :: Interleaved even/odd words
 *  u32 csum[2] :: Even/odd words, ADD.w sum over data
 *
 * TRKTYP_vampires_empire_a data layout:
 *  u8 sector_data[4096]
 *
 * TRKTYP_vampires_empire_b data layout:
 *  u8 sector_data[5120]
 *
 * TRKTYP_vampires_empire_c data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *vampires_empire_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;


        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

       if (s->word == 0x44894489) {
            /* Two sync words is format C. */
            ti->type = TRKTYP_vampires_empire_c;
            ti->data_bitoff = s->index_offset_bc - 31;
        } else if (ti->type == TRKTYP_vampires_empire_c) {
            /* Format B must have two sync words. */
            continue;
        }

       for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (csum != be32toh(sum))
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

static void vampires_empire_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    if (ti->type == TRKTYP_vampires_empire_c)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    else
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
 
    for (i = csum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}



struct track_handler vampires_empire_a_handler = {
    .bytes_per_sector = 4096,
    .nr_sectors = 1,
    .write_raw = vampires_empire_write_raw,
    .read_raw = vampires_empire_read_raw
};

struct track_handler vampires_empire_b_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = vampires_empire_write_raw,
    .read_raw = vampires_empire_read_raw
};

struct track_handler vampires_empire_c_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = vampires_empire_write_raw,
    .read_raw = vampires_empire_read_raw
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
