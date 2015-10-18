/*
 * disk/robocod.c
 * 
 * Custom format as used by "James Pond 2: Codename Robocod" by Millennium.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,0xff,0xff,trknr
 *  u32 csum
 *  u32 data[11][512/4]
 * MFM encoding of sectors:
 *  AmigaDOS-style per-sector encoding (512 bytes even; 512 bytes odd).
 *  AmigaDOS-style checksum over first 10 sectors only! (Rainbird style!)
 * 
 * TRKTYP_robocod data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *robocod_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*512/4], dat[11][512/4], hdr, csum;
        unsigned int i;

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

        for (i = 0; i < ti->nr_sectors; i++) {
            if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 512, raw_dat, dat[i]);
        }
        if (amigados_checksum(dat, 10*512) != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void robocod_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (~0u << 8) | tracknr);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, 10*512)); /* over 10 sectors only! */

    for (i = 0; i < ti->nr_sectors; i++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 512, &dat[i*512/4]);
}

struct track_handler robocod_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = robocod_write_raw,
    .read_raw = robocod_read_raw
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
