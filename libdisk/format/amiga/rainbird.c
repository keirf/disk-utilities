/*
 * disk/rainbird.c
 * 
 * Custom format as used by various Rainbird releases:
 *   Betrayal
 *   Carrier Command
 *   Midwinter
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,0xff,0xff,trknr
 *  u32 csum
 *  u32 data[10*512/4]
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum.
 * 
 * TRKTYP_rainbird data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rainbird_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*ti->len/4], hdr, csum;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw_dat, 16) == -1)
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

        block = memalloc(ti->len);
        memcpy(block, raw_dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void rainbird_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, ti->len));

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler rainbird_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = rainbird_write_raw,
    .read_raw = rainbird_read_raw
};

/* As used on Shockwave by Digital Magic Software. Different data length. */
struct track_handler digital_magic_handler = {
    .bytes_per_sector = 6080,
    .nr_sectors = 1,
    .write_raw = rainbird_write_raw,
    .read_raw = rainbird_read_raw
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
