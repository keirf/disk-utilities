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
#include "private.h"

#include <arpa/inet.h>

#define trknr(t) ((80 * !((t) & 1)) + ((t) >> 1))

static void *blue_byte_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        uint8_t dat[2*(4+6032+2)];

        if (s->word != 0x5542aaaa)
            continue;

        ti->data_bitoff = s->index_offset - 31;

        stream_start_crc(s);
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(MFM_even_odd, 4, dat, dat);
        if ((dat[0] != trknr(tracknr)) || (dat[1] != 1) ||
            (dat[2] != 0) || (dat[3] != 0))
            continue;

        for (i = 0; i < ti->len/4; i++)
            mfm_decode_bytes(MFM_even_odd, 4, &dat[8+8*i], &block[i]);

        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void crc_and_emit_u32(
    struct track_buffer *tbuf, enum mfm_encoding enc,
    uint32_t x, uint16_t *crc)
{
    tbuf_bits(tbuf, SPEED_AVG, enc, 32, x);

    if (enc == MFM_raw) {
        uint16_t y = htons(mfm_decode_bits(MFM_all, x));
        *crc = crc16_ccitt(&y, 2, *crc);
    } else {
        uint32_t i, y;
        for (i = y = 0; i < 32; i++)
            y |= ((x >> i) & 1) << ((i >> 1) + ((i&1)?16:0));
        y = htonl(y);
        *crc = crc16_ccitt(&y, 4, *crc);
    }
}


static void blue_byte_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr = (1u << 16) | (trknr(tracknr) << 24);
    uint32_t *dat = (uint32_t *)ti->dat;
    uint16_t crc = 0xffff;
    unsigned int i;

    crc_and_emit_u32(tbuf, MFM_raw, 0x5542aaaa, &crc);

    crc_and_emit_u32(tbuf, MFM_even_odd, hdr, &crc);

    for (i = 0; i < ti->len/4; i++)
        crc_and_emit_u32(tbuf, MFM_even_odd, ntohl(dat[i]), &crc);

    tbuf_bits(tbuf, SPEED_AVG, MFM_all, 16, crc);
}

struct track_handler blue_byte_handler = {
    .bytes_per_sector = 6032,
    .nr_sectors = 1,
    .write_mfm = blue_byte_write_mfm,
    .read_mfm = blue_byte_read_mfm
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
