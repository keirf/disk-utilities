/*
 * disk/prison.c
 * 
 * Custom format as used by Prison by Chrysalis/Krisalis.
 * 
 * Written in 2015 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,trknr,0x0a,0x09 :: Even/Odd long
 *  u8  zeroes[18]
 *  u8  flakey[512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *prison_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    int seen = 0;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2], hdr;
        uint8_t dat[2][1024];
        unsigned int i;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw_dat, raw_dat);
        hdr = be32toh(raw_dat[0]);
        if (hdr != ((0xff000a09) | (tracknr<<16)))
            continue;

        /* Check for 18 MFM-encoded zero bytes */
        if (stream_next_bytes(s, dat[seen], 36) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 18, dat[seen], dat[seen]);
        for (i = 0; i < 18; i++)
            if (dat[seen][i]) break;
        if (i < 17) /* allow corrupted final byte */
            continue;

        /* Check for flaky bits changing across two revolutions. */
        if (stream_next_bytes(s, dat[seen], 1024) == -1)
            goto fail;
        if (++seen < 2)
            continue;
        if (!memcmp(dat[0], dat[1], 1024))
            goto fail;

        return memalloc(0);
    }

fail:
    return NULL;
}

static void prison_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              0xff000a09 | (tracknr << 16));

    for (i = 0; i < 18; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    tbuf_weak(tbuf, 512*8);
}

struct track_handler prison_handler = {
    .write_raw = prison_write_raw,
    .read_raw = prison_read_raw
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
