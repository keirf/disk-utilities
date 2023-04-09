/*
 * disk/diamenty.c
 * 
 * AmigaDOS-based protection, used on Diamenty by Okay.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * Track is ~101300 bits. The track is standard amiga dos with data 
 * after the 11th sector:
 *  u32 0xAAA5292A   :: Data
 *  u32 0x4445512A   :: Data
 *  U32 u32 dat[64/4] :: includes the decodes long above
 * 
 * TRKTYP_diamenty data layout:
 *  u8 amigados[11][512]
 *  u8 extra_data[64]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *diamenty_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;
    uint32_t dat[64/4], raw[2], sum;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    stream_reset(s);

    while (stream_next_bit(s) != -1) {

        if (s->word != 0xAAA5292A)
            continue;
        raw[0] = be32toh(s->word);

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4445512A)
            continue;
        raw[1] = be32toh(s->word);

        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);
        sum = be32toh(dat[0]);
        for (i = 1; i < 64/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        /* Our own checksum over the data. */
        if (sum != 0xfffffffc)
            continue;

        init_track_info(ti, TRKTYP_diamenty);
        ti->total_bits = 101300;
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

static void diamenty_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)&ti->dat[512*11];
    unsigned int i;

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
    for (i = 0; i < 64/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler diamenty_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = diamenty_write_raw,
    .read_raw = diamenty_read_raw
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
