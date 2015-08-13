/*
 * format/amiga/sextett.c
 * 
 * Sextett compilation by Kingsoft
 *
 * DISK 1:
 * -------
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
 * We simulate this by filling track 161 with 0x9245.
 * 
 * DISK 2:
 * -------
 * Track 161 is entirely filled with 0xaa bytes (MFM 0x4444).
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *sextett_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block;

    /* Tracks 159 & 161: no data, all same data_bitoff (==0) */
    if (tracknr == 159) {
        /* Track 159 is only a protection track on Disk 1. */
        struct track_info *t158 = &d->di->track[158];
        return (t158->type == TRKTYP_sextett_protection) ? memalloc(0) : NULL;
    } else if (tracknr == 161) {
        /* Track 161: Protection on all disks. */
        ti->total_bits &= ~15;
        return memalloc(0);
    }

    /* Disk 1, Track 158: find the key */
    while (stream_next_bit(s) != -1) {
        if (s->word != 0x92459245)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;
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
        /* Disk 1: Key track */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, be32toh(*dat));
    } else if (tracknr == 159) {
        /* Disk 1: Sync track */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x92459245);
    } else if (d->di->track[158].type == TRKTYP_sextett_protection) {
        /* Disk 1: Landing track */
        for (i = 0; i < ti->total_bits/16; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x9245);
    } else {
        /* Disk 2: Pattern track */
        for (i = 0; i < ti->total_bits/8-1; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 4, 0xa);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 4, 0x9); /* discontinuity */
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
