/*
 * disk/pdos_old.c
 * 
 * Custom format as used by Lure Of The Temptress by Revolution / Virgin.
 * Also used by Bill's Tomato Game by Psygnosis.
 * 
 * The format appears to be an early variant of Rob Northen's PDOS.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 * For each sector (12 sectors total):
 *  u8  0x50,0x46,trknr,secnr :: Even/Odd
 *  u32 csum :: Even/Odd AmigaDOS-style checksum
 *  u32 data[512/4] :: Even/Odd
 * 
 * TRKTYP_rnc_pdos_old data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *rnc_pdos_old_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*ti->bytes_per_sector/4];
        uint32_t dat[ti->nr_sectors][ti->bytes_per_sector/4];
        uint32_t hdr, csum;
        unsigned int sec;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (sec = 0; sec < ti->nr_sectors; sec++) {
            if (stream_next_bytes(s, raw_dat, 16) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[0], &hdr);
            mfm_decode_bytes(bc_mfm_even_odd, 4, &raw_dat[2], &csum);
            hdr = be32toh(hdr);
            csum = be32toh(csum);

            if (hdr != ((0x5046u<<16) | (tracknr<<8) | sec))
                break;

            if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector,
                             raw_dat, dat[sec]);
            if (amigados_checksum(dat[sec], ti->bytes_per_sector) != csum)
                break;
        }

        if (sec != ti->nr_sectors)
            goto fail;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 105500;
        return block;
    }

fail:
    return NULL;
}

static void rnc_pdos_old_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int sec;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                  (0x5046u<<16) | (tracknr<<8) | sec);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                  amigados_checksum(dat, ti->bytes_per_sector));
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector,
                   dat);
        dat += ti->bytes_per_sector;
    }
}

struct track_handler rnc_pdos_old_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = rnc_pdos_old_write_raw,
    .read_raw = rnc_pdos_old_read_raw
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
