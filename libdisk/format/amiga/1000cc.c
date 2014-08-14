/*
 * disk/1000cc.c
 *
 * Custom format as used on 1000cc Turbo by Energize/Impressions.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u32 0x2aaaaaaa :: 0
 *  u32 0xaaaaaaaa
 *  u32 length of data :: turbo1000cc_b only
 *  u32 checksum
 *  u32 data[11][512][2]
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *turbo1000cc_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t craw[2], raw[2*512], dat[0x581], csum, sum, chk;
        unsigned int i, sec;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x2aaaaaaa)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        if (stream_next_bytes(s, craw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, craw, &csum);

        if (ti->type == TRKTYP_turbo1000cc_b) {
            if (stream_next_bytes(s, craw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, craw, &chk);
            dat[0x580] = be32toh(chk);
        }

        for (sec = 0; sec < ti->nr_sectors; sec++) {
            if (stream_next_bytes(s, raw, 2*512) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 512, raw, &dat[sec*512/4]);
        }

        for (i = sum = 0; i < ti->len/4; i++)
            sum += be32toh(dat[i]);

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}



static void turbo1000cc_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i, sec;
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    for (i = csum = 0; i < ti->len/4; i++)
        csum += be32toh(dat[i]);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,csum);

    if (ti->type == TRKTYP_turbo1000cc_b)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, dat[0x580]);

    for (sec = 0; sec < ti->nr_sectors; sec++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector,
            &ti->dat[sec*ti->bytes_per_sector]);

}


struct track_handler turbo1000cc_a_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = turbo1000cc_write_raw,
    .read_raw = turbo1000cc_read_raw
};

struct track_handler turbo1000cc_b_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = turbo1000cc_write_raw,
    .read_raw = turbo1000cc_read_raw
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
