/*
 * disk/epic.c
 *
 * Custom format as used on Epic by Ocean.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * Track is ~111000 bits. Track has 11 amigados sectors and
 * one short extra sector. The WHDLoad Install uses DIC
 * to create the image and it is unable to read the disk if
 * the bit length is set greater than 109000 bits.
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x48494849 :: Sync
 *  u32 dat[88/4]
 *
 * TRKTYP_epic data layout:
 *  u8 amigados[11][512]
 *  u8 extra_sector[88]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *epic_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;
    uint32_t dat[88/4], raw[2];
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x48494849)
            continue;

        for (i = 0; i < 88/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        stream_next_index(s);
        init_track_info(ti, TRKTYP_epic);
        /* hard coded the bit length 105500 (Official IPF length) even
           though the actual length is 111500.  If I set the bit length to
           anything above 109000 the image fails when instaloing with the 
           WHDLoad install that uses DIC 
        */
        ti->total_bits = 105500;
        block = memalloc(ti->len + sizeof(dat));
        memcpy(block, ablk, ti->len);
        memcpy(&block[ti->len], dat, sizeof(dat));
        ti->len += sizeof(dat);
        memfree(ablk);
        return block;
    }

fail:
    memfree(ablk);
    return NULL;
}

static void epic_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)&ti->dat[512*11];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x48494849);
    for (i = 0; i < 88/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    for (i = 0; i < 100; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler epic_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = epic_write_raw,
    .read_raw = epic_read_raw
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
