/*
 * disk/platou.c
 *
 * Custom format as used on Platou by Kingsoft.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489  Sync
 *  u32 0xaaaaaaaa
 *  u32 dat[ti->len/4]
 *  u32 raw value - could not find any significants
 * 
 * It does not appear that there is a checksum
 *
 * TRKTYP_platou data layout:
 *  u8 sector_data[6000]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint16_t crcs[];

static void *platou_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4+1];
        unsigned int i;
        char *block;

        if (tracknr != 2 && tracknr != 3 && tracknr != 4  && tracknr != 6)
            goto fail;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (crcs[tracknr-2] != s->crc16_ccitt)
            continue;

        // get last raw u32 and pass it via dat
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        dat[ti->len/4] = s->word;

        stream_next_index(s);
        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void platou_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->len]);
}

struct track_handler platou_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = platou_write_raw,
    .read_raw = platou_read_raw
};

static const uint16_t crcs[] = {
    0x3ee3, 0x250d, 0xd989, 0x0000, 0x1185
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
