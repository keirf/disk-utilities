/*
 * disk/bombuzal.c
 * 
 * AmigaDOS-based long-track protection, used on Bombuzal by Imageworks.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * Track is ~105500 bits. Track begins with a short sector:
 *  u16 0xa145   :: Sync
 *  u16 data[18] :: bc_mfm
 * 
 * Some data from the sector is combined with track 0 & 1 lengths as a
 * checksum.
 * 
 * TRKTYP_bombuzal data layout:
 *  u8 amigados[11][512]
 *  u8 extra_sector[18]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *bombuzal_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;
    uint8_t dat[18];
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    stream_reset(s);

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0xa145)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < sizeof(dat); i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            dat[i] = mfm_decode_word((uint16_t)s->word);
        }

        /* Our own checksum over the data. */
        if (crc16_ccitt(dat, sizeof(dat), 0xffff) != 0x423c)
            continue;

        init_track_info(ti, TRKTYP_bombuzal);
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

static void bombuzal_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)&ti->dat[512*11];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa145);
    for (i = 0; i < 18; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
    for (i = 0; i < 168; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler bombuzal_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = bombuzal_write_raw,
    .read_raw = bombuzal_read_raw
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
