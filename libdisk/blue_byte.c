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
 *  u32 footer :: Raw, probably a duplicator checksum (CRC?)
 * 
 * Track gap is all zeroes.
 * Tracks are enumerated side 1 first, then side 0.
 * Cell timing is 2us as usual (not a long track format)
 * Blue Byte's track loader does not checksum data, hence we have no way
 * to check the data we analyse. Unless the footer value is a checksum.
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
    uint32_t mfm[2], *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x5542aaaa)
            continue;

        ti->data_bitoff = s->index_offset - 31;

        if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
            goto fail;
        mfm_decode_amigados(mfm, 4/4);
        if (ntohl(mfm[0]) != ((1u << 16) | (trknr(tracknr) << 24)))
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, mfm, sizeof(mfm)) == -1)
                goto fail;
            mfm_decode_amigados(mfm, 4/4);
            block[i] = mfm[0];
        }

        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void blue_byte_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t hdr = (1u << 16) | (trknr(tracknr) << 24);
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x5542aaaa);

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, hdr);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, ntohl(dat[i]));
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
