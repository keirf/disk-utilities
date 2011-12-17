/*
 * disk/psygnosis_a.c
 * 
 * Custom format as used by various Psygnosis releases:
 *   Amnios
 *   Aquaventura (sync 0x4429)
 * 
 * Sometimes a single release will use both this and Psygnosis B.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489|0x4429 :: Sync
 *  u32 trk
 *  u32 csum
 *  u32 data[12*512/4]
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum.
 * 
 * TRKTYP_psygnosis_a data layout:
 *  u8 sector_data[12*512]
 *  u16 sync
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *psygnosis_a_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*ti->len/4], hdr, csum;
        uint32_t idx_off = s->index_offset - 15;
        uint16_t sync = s->word;

        if ((sync != 0x4489) && (sync != 0x4429))
            continue;

        ti->data_bitoff = idx_off;

        if (stream_next_bytes(s, raw_dat, 16) == -1)
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

        block = memalloc(ti->len + 2);
        *(uint16_t *)&block[ti->len] = htons(sync);
        memcpy(block, raw_dat, ti->len);
        ti->valid_sectors = (1u << ti->nr_sectors) - 1;
        ti->len += 2; /* for the sync mark */
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
    unsigned int dat_len = ti->len - 2;
    uint16_t sync;

    sync = ntohs(*(uint16_t *)&ti->dat[dat_len]);
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
