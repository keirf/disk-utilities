/*
 * disk/promic_protection.c
 * 
 * AmigaDOS-based protection, used on Laser World and Milioner
 * by Promic Group.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * Track is ~105500 bits. Track begins with a short sector:
 *  u32 0x49114911   :: Sync
 *  u16 data[7] :: bc_raw
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *promic_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char  *block;
    uint16_t sum, dat[7];
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x49114911)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < 0x100/2; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if (mfm_decode_word((uint16_t)s->word) != 0 && mfm_decode_word((uint16_t)s->word) != 0xff)
                goto fail;
        }

        for (i = sum = 0; i < 7; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            dat[i] = (uint16_t)s->word;
            sum += dat[i];
        }

        if (sum != 0x3385)
            continue;

        ti->total_bits = 101400;
        block = memalloc(sizeof(dat));
        memcpy(block, dat, sizeof(dat));
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void promic_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x49114911);
    for (i = 0; i < 0x100/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    for (i = 0; i < 7; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[i]);
}

struct track_handler promic_protection_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = promic_protection_write_raw,
    .read_raw = promic_protection_read_raw
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
