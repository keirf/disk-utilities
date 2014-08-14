/*
 * disk/menace.c
 *
 * Custom format as used on Menace and Blood Money by Psygnosis.
 *
 * Written in 2012 by Keir Fraser
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x552a,0x2a55 :: Sync
 *  u16 dat[0xc1c][2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data (Blood Money)
 *                 eor each decoded word during the sum with tracknr/2
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data (Menace)
 *
 * TRKTYP_dma_design data layout:
 *  u8 sector_data[6200]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *dma_design_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[2], dat[0xc1d], sum, csum, eval;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x552a2a55)
            continue;

        eval = (ti->type == TRKTYP_blood_money) ? (tracknr/2) : 0;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            sum += be16toh(dat[i]) ^ eval;
        }

        csum = eval ^ be16toh(dat[0xc1c]);
        sum -= csum;
        if (csum != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void dma_design_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat, eval;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x552a2a55);

    eval = (ti->type == TRKTYP_blood_money) ? (tracknr/2) : 0;

    for (i = csum = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        csum += be16toh(dat[i]) ^ eval;
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum ^ eval);
}

struct track_handler menace_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = dma_design_write_raw,
    .read_raw = dma_design_read_raw
};

struct track_handler blood_money_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = dma_design_write_raw,
    .read_raw = dma_design_read_raw
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
