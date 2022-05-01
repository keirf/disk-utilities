/*
 * disk/diskspare.c
 *
 * DiskSpare format as used by diskspare.device to provide 12 (DD) or 24 (HD)
 * 512-byte sectors per standard-length track.
 *
 * Written in 2022 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  520 decoded bytes per sector (including sector gap).
 *  12 (or 24) back-to-back sectors, as encoded below (explicit gap included).
 * Decoded Sector:
 *  u8 0x00      :: Sector gap
 *  u8 0xa1,0xa1 :: Sync header (encoded as 0x4489 0x4489)
 *  u8 0x00
 *  u16 csum     :: EOR.w over encoded data
 *  u8 track     :: 0-159
 *  u8 sector    :: 0-{11,23}
 *  u8 data[512]
 */

#include <ctype.h>
#include <libdisk/util.h>
#include <private/disk.h>

static void *diskspare_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int i, nr_valid_blocks = 0;

    block = memalloc(ti->bytes_per_sector * ti->nr_sectors);
    for (i = 0; i < ti->bytes_per_sector * ti->nr_sectors; i += 16)
        memcpy(&block[i], "-=[BAD SECTOR]=-", 16);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint8_t dat[512], raw[512*2];
        uint32_t hdr;
        uint8_t trk, sec;
        uint16_t csum, sum;

        if (s->word != 0x44894489)
            continue;

        if (stream_next_bytes(s, raw, 2*5) == -1)
            break;
        if (raw[0] != 0x2a || raw[1] != 0xaa)
            continue;
        mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[2], &hdr);
        hdr = be32toh(hdr);
        sec = (uint8_t)(hdr >> 16);
        trk = (uint8_t)(hdr >> 24); 
        csum = (uint16_t)hdr;

        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec)
            || (trk != tracknr))
            continue;

        if (stream_next_bytes(s, raw, 512*2) == -1)
            break;
        for (i = 0; i < 512; i += 4)
            mfm_decode_bytes(bc_mfm_even_odd, 4, &raw[i*2], &dat[i]);

        sum = be16toh(((uint16_t *)raw)[0]) & 0x7fff;
        for (i = 1; i < 512; i++)
            sum ^= be16toh(((uint16_t *)raw)[i]);
        if (csum != sum)
            continue;

        memcpy(&block[sec*512], dat, 512);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
    }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    /* Constants taken from Amiga User International, Superdisk No. 56. 
     * User-written disks will not have tracks this long, but it is on the 
     * safer side to use longtracks unconditionally here, as they are easier 
     * to write back (more tolerant of drive speed variance). */
    ti->data_bitoff = 512 * (ti->nr_sectors / 12);
    ti->total_bits = 103000 * (ti->nr_sectors / 12);

    return block;
}

static void diskspare_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = ti->dat;
    unsigned int sec, i;
    uint32_t hdr;
    uint16_t sum, raw[512];

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        for (i = 0; i < 512/4; i++)
            mfm_encode_bytes(bc_mfm_even_odd, 4, &dat[i*4], &raw[i*4],
                             i ? be16toh(raw[i*4-1]) : 0);
        sum = be16toh(raw[0]) & 0x7fff;
        for (i = 1; i < 512; i++)
            sum ^= be16toh(raw[i]);
        hdr = (tracknr << 24) | (sec << 16) | sum;

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        /* sync mark */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        /* pad */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        /* hdr */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
        /* data */
        for (i = 0; i < 512/4; i++)
            tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 4, &dat[i*4]);
        dat += ti->bytes_per_sector;
    }
}

struct track_handler diskspare_dd_handler = {
    .density = trkden_double,
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = diskspare_write_raw,
    .read_raw = diskspare_read_raw,
};

struct track_handler diskspare_hd_handler = {
    .density = trkden_high,
    .bytes_per_sector = 512,
    .nr_sectors = 24,
    .write_raw = diskspare_write_raw,
    .read_raw = diskspare_read_raw
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
