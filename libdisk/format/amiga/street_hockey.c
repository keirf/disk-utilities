/*
 * disk/street_hockey.c
 *
 * Custom format as used on Street Hockey by Gonzo Games
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 Sync
 *  u32 0x4488aaaa
 *  u32 dat[5648/2]
 * 
 * The checksum is part of the decoded data and is calculated 
 * by summing the decoded data dat[b00].  There are ten bytes 
 * of data stored after the checksum.  They are decoded and
 * written back as part of the data.
 *
 * TRKTYP_street_hockey data layout:
 *  u8 sector_data[5648]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *street_hockey_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[ti->len];
        uint32_t sum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4488aaaa)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        for (i = sum = 0; i < ti->len/2-7; i++)
            sum += be16toh(dat[i]);

        if (sum != ((be16toh(dat[0xb01]) << 16) | be16toh(dat[0xb02])))
            continue;

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

static void street_hockey_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint32_t sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4488aaaa);

    for (i = sum = 0; i < ti->len/2-7; i++)
        sum += be16toh(dat[i]);

    dat[0xb01] = htobe16((uint16_t)(sum >> 16));
    dat[0xb02] = htobe16((uint16_t)sum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler street_hockey_handler = {
    .bytes_per_sector = 5648,
    .nr_sectors = 1,
    .write_raw = street_hockey_write_raw,
    .read_raw = street_hockey_read_raw
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
