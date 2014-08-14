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
#include <private/disk.h>

static void *psygnosis_a_write_raw(
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

        ti->data_bitoff = s->index_offset_bc - 15;

        /* Check for second sync mark */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        two_sync = ((uint16_t)s->word == sync);

        /* Read the track number and checksum. If there's no second sync mark, 
         * the first 16 bits of the header info is already streamed. */
        if (stream_next_bits(s, two_sync ? 32 : 16) == -1)
            goto fail;
        raw_dat[0] = htobe32(s->word);
        if (stream_next_bytes(s, &raw_dat[1], 12) == -1)
            goto fail;

        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[0], &hdr);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[2], &csum);
        hdr = be32toh(hdr);
        csum = be32toh(csum);

        if (hdr != (0xffffff00u | tracknr))
            continue;

        if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw_dat, raw_dat);
        if (amigados_checksum(raw_dat, ti->len) != csum)
            continue;

        /* Some titles (Armourgeddon, Obitus...) mastered with long tracks. */
        stream_next_index(s);
        if (s->track_len_bc > 103000)
            ti->total_bits = 105500;

        block = memalloc(ti->len + 4);
        *(uint16_t *)&block[ti->len] = htobe16(sync);
        *(uint16_t *)&block[ti->len+2] = two_sync ? htobe16(sync) : 0;
        memcpy(block, raw_dat, ti->len);
        set_all_sectors_valid(ti);
        ti->len += 4; /* for the sync marks */
        return block;
    }

fail:
    return NULL;
}

static void psygnosis_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int dat_len = ti->len - 4;
    uint16_t sync;

    sync = be16toh(*(uint16_t *)&ti->dat[dat_len]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    sync = be16toh(*(uint16_t *)&ti->dat[dat_len+2]);
    if (sync)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, dat_len));

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, dat_len, dat);
}

struct track_handler psygnosis_a_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = psygnosis_a_write_raw,
    .read_raw = psygnosis_a_read_raw
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
