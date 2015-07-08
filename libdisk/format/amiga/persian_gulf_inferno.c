/*
 * disk/persian_gulf_inferno.c
 * 
 * Custom format as used on Persian Gulf Inferno by Parsec.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x8845 :: Sync
 *  u32 csum
 *  u8[6384][2] :: Interleaved even/odd bytes
 * 
 * NB. Checksum does not include final byte of data region.
 * 
 * TRKTYP_persian_gulf_inferno data layout:
 *  u8 sector_data[6384]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint16_t mk_csum(uint16_t x)
{
    x &= 0x5555;
    x |= ((x^0x5555)<<1)&((x^0x5555)>>1);
    x &= 0x7fff;
    return x;
}

static void *persian_gulf_inferno_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum;
        uint16_t sum;
        uint8_t raw[2], dat[ti->bytes_per_sector];
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x8845)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        csum = s->word;

        sum = 0;
        for (i = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            if (i < 6383)
                sum ^= (raw[0] << 8) | raw[1];
            mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &dat[i]);
        }

        sum += 0x4847;
        if (csum != ((mk_csum(sum>>1) << 16) | mk_csum(sum)))
            continue;

        ti->total_bits = 105500;
        block = memalloc(ti->len);
        memcpy(block, dat, sizeof(dat));
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void persian_gulf_inferno_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint16_t i, sum = 0;
    uint32_t csum;
    struct tbuf csum_tbuf;

    /* Encode the data stream so we can checksum over the MFM data. */
    tbuf_init(&csum_tbuf, 0, 6383*16);
    for (i = 0; i < 6383; i++) {
        tbuf_bits(&csum_tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[i]);
        sum ^= (csum_tbuf.raw.bits[i*2] << 8) | csum_tbuf.raw.bits[i*2+1];
    }
    sum += 0x4847;
    csum = ((mk_csum(sum>>1) << 16) | mk_csum(sum));

    /* If the first clock bit in the data stream is wrong then flip the
     * MSB of the checksum. */
    if ((csum & 1) && (csum_tbuf.raw.bits[0] & 0x80)) {
        sum ^= 0x8000;
        csum = ((mk_csum(sum>>1) << 16) | mk_csum(sum));
    }
    track_purge_raw_buffer(&csum_tbuf.raw);

    /* Now the easy bit: just dump out sync,csum,data... */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8845);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, csum);
    for (i = 0; i < ti->bytes_per_sector; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[i]);
}

struct track_handler persian_gulf_inferno_handler = {
    .bytes_per_sector = 6384,
    .nr_sectors = 1,
    .write_raw = persian_gulf_inferno_write_raw,
    .read_raw = persian_gulf_inferno_read_raw
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
