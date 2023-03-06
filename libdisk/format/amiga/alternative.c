/*
 * disk/alternative.c
 * 
 * AmigaDOS-based protection used by Alternative on the following games:
 * 
 * Dinosaur Detective Agency
 * Postman Pat III
 * Snapperazzi
 * 
 * Written in 2023 by Keith Krellwitz
 *
 * Dinosaur Detective Agency
 *  Track is standard length amigados, but has the data offset between 0x90
 *  and 0xd0 words.  The offset is set to 0xb0 words or 2816 bits
 *  
 * Postman Pat III
 *  Track is standard length amigados, but has the data offset > 0xc0
 * 
 * Snapperazzi:
 *  Track is standard length amigados, but has the data offset between 0x1a8
 *  and 0x1e0 words.  The offset is set to 0x1c5 words or 7248 bits
 * 

 * TRKTYP_snapperazzi data layout:
 *  u8 amigados[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct alternative_info {
    uint32_t checksum;
    uint32_t data_bitoff;
};

const static struct alternative_info alternative_infos[] = {
    { 0x388edf6a, 7248 },  // Snapperazzi
    { 0x4bdfbe5a, 2816 },  // Dinosaur Detective Agency
    { 0xd48fe00c, 3984 }   // Postman Pat III
};

static const struct alternative_info *find_alternative_info(uint32_t checksum)
{
    const struct alternative_info *alternative_info;
    unsigned int count = 0;
    for (alternative_info = alternative_infos; alternative_info->checksum != checksum; alternative_info++){
        count++;
        if (count == sizeof(alternative_infos)/sizeof(*alternative_infos))
            return NULL;
        continue;
    }
    return alternative_info;
}

static void *alternative_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
   
    char *ablk, *block;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    /* calculate the checks using eor and do a look up to get the
       data offset
    */
    uint32_t sum, *dat = (uint32_t *)ablk;
    for (i = sum = 0; i < ti->len/4; i++)
        sum ^= be32toh(dat[i]);

    const struct alternative_info *alternative_info = find_alternative_info(sum);

    /* If checksum not found then fail */
    if (alternative_info == NULL)
        goto fail;

    init_track_info(ti, TRKTYP_alternative);
    ti->data_bitoff = alternative_info->data_bitoff;
    block = memalloc(ti->len);
    memcpy(block, ablk, ti->len);
    memfree(ablk);
    return block;

fail:
    memfree(ablk);
    return NULL;
}

static void alternative_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler alternative_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = alternative_write_raw,
    .read_raw = alternative_read_raw
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
