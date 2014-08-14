/*
 * disk/bat.c
 * 
 * Custom format as used on B.A.T. by Ubisoft.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8945
 *  u32 data_even[0x628]
 *  u32 csum_even
 *  u32 data_odd[0x628]
 *  u32 csum_odd
 *  Checksum is sum of all decoded longs.
 *  Track length is usual long (~105500 bitcells)
 * 
 * TRKTYP_bat data layout:
 *  u8 sector_data[6304]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *bat_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, dat[0x629*2];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x8945)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, sizeof(dat)/2, dat, dat);

        csum = tracknr ^ 1;
        for (i = 0; i < 0x628; i++)
            csum += be32toh(dat[i]);
        if (csum != be32toh(dat[0x628]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

    return NULL;
}

static void bat_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[0x629];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8945);

    memcpy(dat, ti->dat, ti->len);
    csum = tracknr ^ 1;
    for (i = 0; i < 0x628; i++)
        csum += be32toh(dat[i]);
    dat[0x628] = htobe32(csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 0x629*4, dat);
}

struct track_handler bat_handler = {
    .bytes_per_sector = 6304,
    .nr_sectors = 1,
    .write_raw = bat_write_raw,
    .read_raw = bat_read_raw
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
