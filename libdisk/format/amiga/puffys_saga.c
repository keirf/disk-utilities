/*
 * disk/puffys_saga.c
 * 
 * Custom format as used on Puffy's Saga by Ubisoft.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u16 zero[60]      :: No encoding interleave
 *  u16 0x4444
 *  u16 csum[2]       :: ADD.W sum over remaining words
 *  u16 cyl[2]
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data
 * 
 * TRKTYP_puffys_saga data layout:
 *  u8 sector_data[5632]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *puffys_saga_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[2*2818], csum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < 30; i++) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (mfm_decode_word(s->word))
                break;
        }
        if (i != 30)
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4444)
            continue;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, sizeof(dat)/2, dat, dat);

        csum = 0;
        for (i = 1; i < 2818; i++)
            csum += be16toh(dat[i]);
        if ((be16toh(dat[0]) != csum) || (be16toh(dat[1]) != (tracknr/2)))
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat[2], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void puffys_saga_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    for (i = 0; i < 30; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xaa);

    csum = tracknr/2;
    for (i = 0; i < ti->len/2; i++)
        csum += be16toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, csum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, tracknr/2);

    for (i = 0; i < ti->len/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, be16toh(dat[i]));
}

struct track_handler puffys_saga_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = puffys_saga_write_raw,
    .read_raw = puffys_saga_read_raw
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
