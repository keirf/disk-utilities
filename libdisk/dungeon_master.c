/*
 * disk/dungeon_master.c
 * 
 * An Atari ST (i.e., IBM-compatible) MFM track with weak bits in sector 1.
 * 
 * The protection relies on an ambiguous flux transition at the edge of the
 * FDC's inspection window, which may be interpreted as clock or as data.
 * Thus the MSB of each byte in the weak area is randomly read as 0 or 1.
 * 
 * Note that this relies on fairly authentic PLL behaviour in the flux
 * decoder, to respond slowly to 'out of sync' pulses. Else we can lose sync
 * with the bit stream.
 * 
 * Notes on IBM-compatible MFM data format:
 * ----------------------------------------
 * Supported by uPD765A, Intel 8272, and many other FDC chips, as used in
 * pretty much every home computer (except Amiga and C64!).
 * 
 * One of the more useful references:
 *  "uPD765A/7265 Single/Double Density Floppy Disk Controllers",
 *  NEC Electronics Inc.
 * 
 * Index Address Mark (IAM):
 *      0xc2c2c2fc
 * ID Address Mark (IDAM):
 *      0xa1a1a1fe, <cyl>, <hd> <sec>, <sz>, <crc16_ccitt>
 * Data Address Mark (DAM):
 *      0xa1a1a1fb, <N bytes data>, <crc16_ccitt> [N = 128 << sz]
 * Deleted Data Address Mark (DDAM):
 *      As DAM, but identifier 0xfb -> 0xf8
 * 
 * NB. In above, 0xc2 and 0xa1 are sync marks which have one of their clock
 *     bits forced to zero. Hence 0xc2 -> 0x5224; 0xa1 -> 0x4489.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *dungeon_master_weak_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->bytes_per_sector * ti->nr_sectors);
    unsigned int valid_blocks = 0;

    /* Fill value for all sectors seems to be 0xe5. */
    memset(block, 0xe5, ti->bytes_per_sector * ti->nr_sectors);

    while ((stream_next_bit(s) != -1) &&
           (valid_blocks != ((1u<<ti->nr_sectors)-1))) {

        uint32_t sz, idx_off = s->index_offset - 31;
        uint8_t dat[2*514], cyl, head, sec, no;
        uint16_t crc;
        unsigned int i;

        /* IDAM */
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            break;
        if (s->word != 0x44895554)
            continue;

        if (stream_next_bits(s, 32) == -1)
            break;
        cyl = mfm_decode_bits(MFM_all, s->word >> 16);
        head = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            break;
        sec = mfm_decode_bits(MFM_all, s->word >> 16);
        no = mfm_decode_bits(MFM_all, s->word);
        if (stream_next_bits(s, 32) == -1)
            break;
        sz = 128 << no;
        if ((cyl != 0) || (head != 1) || (sz != 512) || (s->crc16_ccitt != 0))
            continue;

        sec--;
        if ((sec >= ti->nr_sectors) || (valid_blocks & (1u<<sec)))
            continue;

        /* DAM */
        while (stream_next_bit(s) != -1)
            if (s->word == 0x44894489)
                break;
        if (s->word != 0x44894489)
            continue;
        stream_start_crc(s);
        if (stream_next_bits(s, 32) == -1)
            break;
        if (s->word != 0x44895545)
            continue;
        crc = s->crc16_ccitt;

        /*
         * Sector 0 weak-bit protection relies on authentic behaviour of FDC 
         * PLL to respond slowly to marginal bits at edge of inspection window.
         */
        stream_authentic_pll(s, (sec == 0));
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        stream_authentic_pll(s, 0);
        mfm_decode_bytes(MFM_all, 514, dat, dat);

        if (sec == 0) {
            /*
             * Check each flakey byte is read as 0x68 or 0xE8. Rewrite as
             * originally mastered (always 0x68, with timing variation).
             * Any mismatching bytes will cause us to fail the CRC check.
             */
            for (i = 20; i < 509; i++)
                if ((dat[i]&0x7f) == 0x68)
                    dat[i] = 0x68;
            /* Re-compute the CRC on fixed-up data. */
            s->crc16_ccitt = crc16_ccitt(dat, 514, crc);
        }

        if (s->crc16_ccitt != 0)
            continue;

        memcpy(&block[sec*512], dat, 512);
        valid_blocks |= 1u << sec;
        if (sec == 0)
            ti->data_bitoff = idx_off;
    }

    /* Must have found valid sector 0 */
    if (!(valid_blocks & 1u)) {
        memfree(block);
        return NULL;
    }

    ti->valid_sectors = valid_blocks;

    return block;
}

static void dungeon_master_weak_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = 0, hd = 1, no = 2;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        /* IDAM */
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, sec+1);
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);

        /* DAM */
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 32, 0x44895545);
        tbuf_bytes(tbuf, SPEED_AVG, MFM_all, 512, &dat[sec*512]);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 40; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x4e);
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0x00);
    }
}

struct track_handler dungeon_master_weak_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_mfm = dungeon_master_weak_write_mfm,
    .read_mfm = dungeon_master_weak_read_mfm
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
