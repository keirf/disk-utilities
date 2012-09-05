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
#include "../private.h"

#include <arpa/inet.h>

static void *bat_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, dat[0x629*2];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x8945)
            continue;

        ti->data_bitoff = s->index_offset - 15;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(MFM_even_odd, sizeof(dat)/2, dat, dat);

        csum = tracknr ^ 1;
        for (i = 0; i < 0x628; i++)
            csum += ntohl(dat[i]);
        if (csum != ntohl(dat[0x628]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->total_bits = 105500;
        return block;
    }

    return NULL;
}

static void bat_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[0x629];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x8945);

    memcpy(dat, ti->dat, ti->len);
    csum = tracknr ^ 1;
    for (i = 0; i < 0x628; i++)
        csum += ntohl(dat[i]);
    dat[0x628] = htonl(csum);

    tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, 0x629*4, dat);
}

struct track_handler bat_handler = {
    .bytes_per_sector = 6304,
    .nr_sectors = 1,
    .write_mfm = bat_write_mfm,
    .read_mfm = bat_read_mfm
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
