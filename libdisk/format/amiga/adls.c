/*
 * disk/adls.c
 * 
 * Argonaut Dual Loading System (ADLS) as used (solely!) on Starglider 2.
 * 
 * IBM-MFM format, with special sector numbers and sizes, and modified IDAM
 * contents for the Amiga data tracks.
 * 
 * Each track contains 5 sectors (0xf5-0xf9) of 1024 bytes, and 1 sector (0xfa)
 * of 512 bytes. Some ST tracks appear to be missing the short sector.
 * 
 * TODO: Track 0 contains Atari ST boot sector. Decode this.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *adls_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(5*1024+512);
    unsigned int nr_valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        int idx_off;
        unsigned int sz;
        uint8_t dat[2*(1024+2)];
        struct ibm_idam idam;

        /* IDAM */
        if ((idx_off = ibm_scan_idam(s, &idam)) < 0)
            continue;
        if (s->crc16_ccitt != 0)
            continue;

        idam.sec -= 0xf5;
        if ((idam.sec >= ti->nr_sectors) || is_valid_sector(ti, idam.sec))
            continue;
        sz = (idam.sec == 5) ? 512 : 1024;

        if (tracknr & 1) {
            if ((idam.cyl != (tracknr/2)) || (idam.head != (tracknr&1)) ||
                (idam.no != ((idam.sec == 5) ? 2 : 3)))
                continue;
        } else {
            if ((idam.cyl != 0xf7) || (idam.head != 0xf7) ||
                (idam.no != ((idam.sec == 5) ? 0xf6 : 0xf7)))
                continue;
        }

        /* DAM */
        if (ibm_scan_dam(s) < 0)
            continue;
        if (stream_next_bytes(s, dat, 2*(sz+2)) == -1)
            goto out;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(bc_mfm, sz, dat, dat);
        memcpy(&block[idam.sec*1024], dat, sz);
        set_sector_valid(ti, idam.sec);
        nr_valid_blocks++;
        if (idam.sec == 0)
            ti->data_bitoff = idx_off;
    }

out:
    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    ti->data_bitoff = 80 * 16;

    return block;
}

static void adls_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = tracknr/2, hd = tracknr&1, no = 2;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        if (tracknr & 1) {
            cyl = tracknr/2;
            hd = tracknr&1;
            no = (sec == 5) ? 2 : 3;
        } else {
            cyl = hd = 0xf7;
            no = (sec == 5) ? 0xf6 : 0xf7;
        }

        /* IDAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sec+0xf5);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, (sec == 5) ? 512 : 1024,
                   &dat[sec*1024]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 24; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
    }
}

struct track_handler adls_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 6,
    .write_raw = adls_write_raw,
    .read_raw = adls_read_raw
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
