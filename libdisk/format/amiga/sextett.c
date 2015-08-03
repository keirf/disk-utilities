/*
 * format/amiga/sextett.c
 * 
 * Sextett compilation by Kingsoft
 *
 * Track 158:
 *  u16 0x92459245
 *  u8 key[3] :: raw bytes
 *  Track gap is zeroes.
 * Track 159:
 *  u16 0x92459245
 * Protection check synchronises on T159, then steps to T161 while
 * simultaneously starting an unsynchronised read DMA. The key from T158 is
 * used as an offset into the read buffer, which must contain a 0x9245 sync
 * word at that offset.
 * We simulate this by filling tracks 160 and 161 with 0x9245.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sextett_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block;

    while (stream_next_bit(s) != -1) {
        if (s->word != 0x92459245)
            continue;
        if (tracknr == 158)
            ti->data_bitoff = s->index_offset_bc - 31;
        else /* Tracks 159-161: no data, all same data_bitoff (==0) */
            return memalloc(0);
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
    unsigned int i;

    if (tracknr == 158) {
        /* Key track */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, be32toh(*dat));
    } else if (tracknr == 159) {
        /* Sync track */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
    } else {
        /* Landing track */
        for (i = 0; i < 3000; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
    }
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
