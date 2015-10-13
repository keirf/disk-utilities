/*
 * disk/cyberblast.c
 * 
 * Custom format as used on Cyberblast by Innerprise.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4448,0xa5a4,0xa5a4 :: Sync
 *  u32 csum,track
 *  u32 data[0x600]
 * 
 * Checksum is EOR of all non-sync longs.
 * 
 * TRKTYP_cyberblast data layout:
 *  u8 sector_data[12*512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *cyberblast_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block = memalloc(ti->len);
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        uint32_t csum, dat[0x602], raw[2], enctrk;

        if (s->word != 0x4448a5a4)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != 0xa5a4a5a4)
            continue;

        for (i = csum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            csum ^= dat[i];
        }

        enctrk = (tracknr < 80) ? tracknr-2 : tracknr-14;
        if ((csum != 0) || (be32toh(dat[1]) != enctrk))
            continue;

        memcpy(block, &dat[2], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void cyberblast_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t enctrk, sum, *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4448);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa5a4a5a4);

    enctrk = sum = (tracknr < 80) ? tracknr-2 : tracknr-14;
    for (i = 0; i < ti->len/4; i++)
        sum ^= be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, enctrk);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler cyberblast_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = cyberblast_write_raw,
    .read_raw = cyberblast_read_raw
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
