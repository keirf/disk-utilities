/*
 * disk/blue_byte.c
 * 
 * Custom format as used by various Blue Byte releases:
 *   Great Courts
 *   Pro Tennis Tour
 *   Twinworld
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x5542aaaa :: Sync
 *  u8  trknr
 *  u8  1,0,0
 *  u32 data[6032/4]
 *  u16 crc_ccitt  :: Over all track contents, in order
 * 
 * Track gap is all zeroes.
 * Tracks are enumerated side 1 first, then side 0.
 * Cell timing is 2us as usual (not a long track format)
 * 
 * MFM encoding:
 *  Alternating even/odd longs
 * 
 * TRKTYP_blue_byte data layout:
 *  u8 sector_data[6032]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define trknr(t) ((80 * !((t) & 1)) + ((t) >> 1))

static void *blue_byte_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        uint8_t dat[2*(4+6032+2)];

        if (s->word != 0x5542aaaa)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        stream_start_crc(s);
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(bc_mfm_even_odd, 4, dat, dat);
        if ((dat[0] != trknr(tracknr)) || (dat[1] != 1) ||
            (dat[2] != 0) || (dat[3] != 0))
            continue;

        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(bc_mfm_even_odd, 4, &dat[8+8*i], &block[i]);

        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void blue_byte_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr = (1u << 16) | (trknr(tracknr) << 24);
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_start_crc(tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5542);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler blue_byte_handler = {
    .bytes_per_sector = 6032,
    .nr_sectors = 1,
    .write_raw = blue_byte_write_raw,
    .read_raw = blue_byte_read_raw
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
