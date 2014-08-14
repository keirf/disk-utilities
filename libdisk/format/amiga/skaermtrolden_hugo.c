/*
 * disk/skaermtrolden_hugo.c
 *
 * Custom format as used by Skaermtrolden Hugo by Silverrock Productions.
 *
 * Written in 2014 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489|0x89448944 :: Sync (even|odd tracks)
 *  u32 csum (ADD.L checksum over data)
 *  u32 disk_nr
 *  u16 track_nr,track_nr
 *  u32 data[5940/4]
 * 
 * NB. Checksum straight after 4489 sync word can cause an unwanted sync match
 * during DMA (e.g, consider MFM 4489 4489 12..). This game avoids that by
 * encoding an initial 0 as MFM 10, but this can be hard to read from old disks
 * and we get MFM 01 instead. This can result in the checksum MSB being read as
 * 1 when it should be 0!
 * 
 * We detect and allow for this in the parser. When regenerating the MFM we
 * sidestep the issue by writing the second sync word as 448a. The game does
 * not check the second sync.
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *skaermtrolden_hugo_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag_disk_nr = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);

    while (stream_next_bit(s) != -1) {

        uint32_t csum, sum, disk, trk, raw[5940/2];
        unsigned int i;
        char *block;
        int bad_original_sync;

        /* Accept our own rewritten second sync word (4489 -> 448a). */
        if ((s->word&~3) != ((tracknr & 1) ? 0x89448944 : 0x44894488))
            continue;
        bad_original_sync = (s->word == 0x44894489);

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 24) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[0], &csum);
        csum = be32toh(csum);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2], &disk);
        disk = be32toh(disk);
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[4], &trk);
        trk = be32toh(trk);

        if (!disktag_disk_nr)
            disktag_disk_nr = (struct disktag_disk_nr *)
                disk_set_tag(d, DSKTAG_disk_nr, 4, &disk);
        if (disk != disktag_disk_nr->disk_nr)
            continue;

        if (trk != ((tracknr<<16) | tracknr))
            continue;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        for (i = sum = 0; i < 5940/4; i++) {
            mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2*i], &raw[i]);
            sum += be32toh(raw[i]);
        }

        /* See header comment for why we accept incorrect checksum MSB. */
        if ((sum != csum) &&
            !(bad_original_sync && (sum == (csum & 0x7fffffff))))
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void skaermtrolden_hugo_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    unsigned int i;
    struct disktag_disk_nr *disktag_disk_nr = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);

    for (i = csum = 0; i < 5940/4; i++)
        csum += be32toh(dat[i]);

    /* NB. Second 4489 sync word modified to 448a to avoid sync issue above. */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32,
              (tracknr & 1) ? 0x89448944 : 0x4489448a);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, disktag_disk_nr->disk_nr);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, (tracknr<<16) | tracknr);

    for (i = 0; i < 5940/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler skaermtrolden_hugo_handler = {
    .bytes_per_sector = 5940,
    .nr_sectors = 1,
    .write_raw = skaermtrolden_hugo_write_raw,
    .read_raw = skaermtrolden_hugo_read_raw
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
