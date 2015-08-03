/*
 * format/amiga/sextett.c
 * 
 * Sextett compilation by Kingsoft
 *
 * u16 0x92459245
 * u8 key[3] :: raw bytes
 * Track gap is zeroes.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sextett_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block;

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 31;
        if (s->word != 0x92459245)
            continue;
        if (stream_next_bits(s, 32) == -1)
            break;
        block = memalloc(4);
        *block = htobe32(s->word);
        return block;
    }

    return NULL;
}

static void sextett_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, be32toh(*dat));
}

struct track_handler sextett_protection_handler = {
    .write_raw = sextett_protection_write_raw,
    .read_raw = sextett_protection_read_raw
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
