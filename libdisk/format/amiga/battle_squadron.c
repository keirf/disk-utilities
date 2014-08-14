/*
 * disk/battle_squadron.c
 *
 * Custom format as used on Battle Squadron by Innerprise.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x#### :: Sync 7 possible syncs (calculated by track_sync)
 *  u16 0x#### :: Sync
 *  u32 0x2aaaaaaa or 0xaaaaaaaa
 *  u32 0x4aaaa44a
 *  u32 track :: track number
 *  u32 csum :: checksum
 *  u32 dat[6144/4]
 *
 * TRKTYP_battle_squadron data layout:
 *  u8 sector_data[6144]
 */


#include <libdisk/util.h>
#include <private/disk.h>

static uint16_t track_sync(uint32_t tracknr)
{
    unsigned int i;
    uint16_t sync, trk;
    sync = 0x4854;
    trk = tracknr & 7;

    for (i = 0; i < trk; i++)
        sync = (sync>>1) | (sync<<15);

    return sync;
}

static void *battle_squadron_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t csum, sum, raw[2*(ti->len+8)/4], dat[(ti->len+8)/4];
        uint16_t sync;
        unsigned int i;
        char *block;

        sync = track_sync(tracknr);

        if ((uint16_t)s->word !=  sync )
            continue;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if (s->word != ((uint32_t)sync<<16 | sync))
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        /* The next long is never used and is either
         * 0x2aaaaaaa or 0xaaaaaaaa on the NTSC release
         */
        if (stream_next_bits(s, 32) == -1)
            goto fail;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4aaaa44a)
            continue;

        if (stream_next_bytes(s, raw, 2*(ti->len+8)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, (ti->len+8), raw, dat);

        sum = 0;
        for (i = 2; i < 0x1808/4; i++)
             sum ^= be32toh(dat[i]);

        csum = be32toh(dat[0])^be32toh(dat[1]);
        if (sum != csum)
            continue;

        /* Some releases use long tracks  */
        stream_next_index(s);
        ti->total_bits = (s->track_len_bc > 101000) ? 103200
            : 100500;

        block = memalloc(ti->len);
        memcpy(block, &dat[2], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void battle_squadron_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[(ti->len+8)/4];
    unsigned int i;
    uint16_t sync;

    sync = track_sync(tracknr);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4aaaa44a);

    dat[0] = htobe32(tracknr<<16);
    memcpy(&dat[2], ti->dat, ti->len);

    csum = 0;
    for (i = 2; i < (ti->len+8)/4; i++)
        csum ^= be32toh(dat[i]);
    dat[1] = htobe32(csum^be32toh(dat[0]));

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len+8, dat);
}

struct track_handler battle_squadron_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = battle_squadron_write_raw,
    .read_raw = battle_squadron_read_raw
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
