/*
 * disk/snapperazzi.c
 * 
 * AmigaDOS-based protection, used on Snapperazzi by Alternative.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * Track is standard length amigados, but has the data offset between 0x1a8
 * and 0x1e0 words.  The offset is set to 0x1c5 words or 7248 bits
 * 
 * 
 * TRKTYP_snapperazzi data layout:
 *  u8 amigados[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *snapperazzi_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    /* calculate the checks using eor and compare to the one calculated 
    when creating the decoder.  Tested with the Official IPF and the dump
    from BarryB */
    uint32_t sum, *dat = (uint32_t *)ablk;
    for (i = sum = 0; i < ti->len/4; i++)
        sum ^= be32toh(dat[i]);
    if (sum != 0x388edf6a)
        goto fail;

    init_track_info(ti, TRKTYP_snapperazzi);
    ti->data_bitoff = 7248;
    block = memalloc(ti->len);
    memcpy(block, ablk, ti->len);
    memfree(ablk);
    return block;

fail:
    memfree(ablk);
    return NULL;
}

static void snapperazzi_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler snapperazzi_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = snapperazzi_write_raw,
    .read_raw = snapperazzi_read_raw
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
