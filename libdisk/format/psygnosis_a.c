/*
 * disk/psygnosis_a.c
 * 
 * Custom format as used by various Psygnosis releases:
 *   Amnios
 *   Aquaventura (sync 0x4429)
 *   Obitus (sync 0x44294429)
 * 
 * Sometimes a single release will use both this and Psygnosis B.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489|0x4429 :: Sync (possibly x2)
 *  u32 trk
 *  u32 csum
 *  u32 data[12*512/4]
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum.
 * 
 * TRKTYP_psygnosis_a data layout:
 *  u8 sector_data[12*512]
 *  u16 sync1,sync2
 */

#include <libdisk/util.h>
#include "../private.h"

#include <arpa/inet.h>

static void *psygnosis_a_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*ti->len/4], hdr, csum;
        uint16_t sync = s->word;
        bool_t two_sync;

        if ((sync != 0x4489) && (sync != 0x4429))
            continue;

        ti->data_bitoff = s->index_offset - 15;

        /* Check for second sync mark */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        two_sync = ((uint16_t)s->word == sync);

        /*
         * Read the track number and checksum. If there's no second sync
         * mark, the first 16 bits of the header info is already streamed.
         */
        if (stream_next_bits(s, two_sync ? 32 : 16) == -1)
            goto fail;
        raw_dat[0] = htonl(s->word);
        if (stream_next_bytes(s, &raw_dat[1], 12) == -1)
            goto fail;

        mfm_decode_bytes(MFM_even_odd, 4, &raw_dat[0], &hdr);
        mfm_decode_bytes(MFM_even_odd, 4, &raw_dat[2], &csum);
        hdr = ntohl(hdr);
        csum = ntohl(csum);

        if (hdr != (0xffffff00u | tracknr))
            continue;

        if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
            goto fail;
        mfm_decode_bytes(MFM_even_odd, ti->len, raw_dat, raw_dat);
        if (amigados_checksum(raw_dat, ti->len) != csum)
            continue;

        block = memalloc(ti->len + 4);
        *(uint16_t *)&block[ti->len] = htons(sync);
        *(uint16_t *)&block[ti->len+2] = two_sync ? htons(sync) : 0;
        memcpy(block, raw_dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->len += 4; /* for the sync marks */
        return block;
    }

fail:
    return NULL;
}

static void psygnosis_a_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int dat_len = ti->len - 4;
    uint16_t sync;

    sync = ntohs(*(uint16_t *)&ti->dat[dat_len]);
    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, sync);
    sync = ntohs(*(uint16_t *)&ti->dat[dat_len+2]);
    if (sync)
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, sync);

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32,
              amigados_checksum(dat, dat_len));

    tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, dat_len, dat);
}

struct track_handler psygnosis_a_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_mfm = psygnosis_a_write_mfm,
    .read_mfm = psygnosis_a_read_mfm
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
