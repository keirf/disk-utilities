/*
 * disk/tank_buster.c
 *
 * Custom format as used on Tank Buster by Kingsoft
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 Multiple Syncs 
 *      (0xa244, 0x4489, 0x2891, 0x9448, 0x2244)
 *  u32 0xaaaaaaaa
 *  u32 dat[ti->len/4]
 * 
 * The tracks do not containn a checksum
 *
 * TRKTYP_tank_buster data layout:
 *  u8 sector_data[5120]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint16_t get_sync(unsigned int tracknr) {
    if (tracknr == 34)
        return 0xa244;
    else if ((tracknr >= 2 && tracknr <= 33) || (tracknr >= 71 && tracknr <= 121))
        return 0x4489;
    else if (tracknr >= 52 && tracknr <= 70 )
        return 0x2891;
    else if (tracknr >= 35 && tracknr <= 50 )
        return 0x9448;
    else if (tracknr == 1 || tracknr == 51 )
        return 0x2244;
    return 0x2244;
}

static void *tank_buster_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != get_sync(tracknr))
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void tank_buster_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, get_sync(tracknr));
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler tank_buster_handler = {
    .bytes_per_sector = 6300,
    .nr_sectors = 1,
    .write_raw = tank_buster_write_raw,
    .read_raw = tank_buster_read_raw
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
