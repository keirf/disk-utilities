/*
 * intact.c
 * 
 * Custom format as used on Intact by Sphinx Software / Grandslam.
 * 
 * Written in 2021 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint16_t syncword(unsigned int tracknr)
{
    if ((tracknr < 88) || (tracknr > 119))
        return 0xc630;
    return (tracknr == 118) ? 0x4509 : 0x88c8;
}

static void *intact_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int i;
    uint16_t sync = syncword(tracknr);

    while (stream_next_bit(s) != -1) {

        uint32_t raw;
        uint16_t dat[0xbb9], csum;

        if (((uint16_t)s->word != sync) || ((s->word>>16) != 0xaaaa))
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x88888888)
            continue;

        for (i = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            raw = htobe32(s->word);
            mfm_decode_bytes(bc_mfm_even_odd, 2, &raw, &dat[i]);
        }

        csum = 0;
        for (i = 0; i < ARRAY_SIZE(dat)-1; i++)
            csum += be16toh(dat[i]);
        if (csum == be16toh(dat[i])) {
            block = memalloc(ti->len);
            memcpy(block, dat, ti->len);
            set_all_sectors_valid(ti);
            return block;
        }
    }

fail:
    return NULL;

}

static void intact_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat, csum = 0;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncword(tracknr));
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x88888888);

    for (i = 0; i < ti->len/2; i++) {
        uint16_t x = be16toh(dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, x);
        csum += x;
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);
}

struct track_handler intact_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = intact_write_raw,
    .read_raw = intact_read_raw
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
