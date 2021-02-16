/*
 * disk/ghost_battle.c
 * 
 * Custom format as used on Ghost Battle by Thalion.
 * 
 * Written in 2021 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 csum[2]       :: AmigaDOS style
 *  u32 data[6240/4][2]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *ghost_battle_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[6240/4+1];
        unsigned int i;
        char *block;

        if (s->word != 0x55554489)
            continue;

        for (i = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (amigados_checksum(&dat[1], 6240) != be32toh(dat[0]))
            continue;

        ti->bytes_per_sector = ti->len;
        block = memalloc(ti->len);
        memcpy(block, &dat[1], ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101400;
        /* Slightly later data write means that write splice should not
         * appear in the game protection check. Scans from mfm_buf+12556
         * which is from +100448 bits after the 4489 sync.
         * 400+38*16+100448 = 101456 > 101400 (track length) */
        ti->data_bitoff = 400;
        return block;
    }

fail:
    return NULL;
}

static void ghost_battle_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_set_gap_fill_byte(tbuf, 0xff);

    /* GAP key */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x9f);

    /* Padding */
    for (i = 0; i < 36; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);

    /* Sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    /* Checksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              amigados_checksum(dat, 6240));

    /* Data */
    for (i = 0; i < 6240/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler ghost_battle_handler = {
    .nr_sectors = 1,
    .bytes_per_sector = 6240,
    .write_raw = ghost_battle_write_raw,
    .read_raw = ghost_battle_read_raw
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
