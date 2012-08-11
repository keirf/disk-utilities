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
#include "private.h"

#include <arpa/inet.h>

static void *adls_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(5*1024+512);
    unsigned int valid_blocks = 0;

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        uint32_t sz, idx_off = s->index_offset - 31;
        uint8_t dat[2*(1024+2)], cyl, head, sec, no;

        /* IDAM */
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        if (s->word != 0x44895554)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto out;
        cyl = mfm_decode_bits(MFM_all, s->word >> 16);
        head = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        sec = mfm_decode_bits(MFM_all, s->word >> 16);
        no = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        if (s->crc16_ccitt != 0)
            continue;

        sec -= 0xf5;
        if ((sec >= ti->nr_sectors) || (valid_blocks & (1u<<sec)))
            continue;
        sz = (sec == 5) ? 512 : 1024;

        if (tracknr & 1) {
            if ((cyl != (tracknr/2)) || (head != (tracknr&1)) ||
                (no != ((sec == 5) ? 2 : 3)))
                continue;
        } else {
            if ((cyl != 0xf7) || (head != 0xf7) ||
                (no != ((sec == 5) ? 0xf6 : 0xf7)))
                continue;
        }

        /* DAM */
        while (stream_next_bit(s) != -1)
            if (s->word == 0x44894489)
                break;
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            goto out;
        if (s->word != 0x44895545)
            continue;
        if (stream_next_bytes(s, dat, 2*(sz+2)) == -1)
            goto out;
        if (s->crc16_ccitt != 0)
            continue;

        mfm_decode_bytes(MFM_all, sz, dat, dat);
        memcpy(&block[sec*1024], dat, sz);
        valid_blocks |= 1u << sec;
        if (sec == 0)
            ti->data_bitoff = idx_off;
    }

out:
    if (!valid_blocks) {
        memfree(block);
        return NULL;
    }

    ti->data_bitoff = 80 * 16;
    ti->valid_sectors = valid_blocks;

    return block;
}

static void adls_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
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
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, sec+0xf5);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);

        /* DAM */
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, MFM_all, (sec == 5) ? 512 : 1024,
                   &dat[sec*1024]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 40; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
    }
}

struct track_handler adls_handler = {
    .bytes_per_sector = 1024,
    .nr_sectors = 6,
    .write_mfm = adls_write_mfm,
    .read_mfm = adls_read_mfm
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
