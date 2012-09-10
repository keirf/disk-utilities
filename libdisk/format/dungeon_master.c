/*
 * disk/dungeon_master.c
 * 
 * An Atari ST (i.e., IBM-compatible) MFM track with weak bits in sector 1.
 * Also support Chaos Strikes Back, featuring weak bits in sector 2.
 * 
 * The protection relies on an ambiguous flux transition at the edge of the
 * FDC's inspection window, which may be interpreted as clock or as data.
 * Thus the MSB of each byte in the weak area is randomly read as 0 or 1.
 * 
 * Note that this relies on fairly authentic PLL behaviour in the flux
 * decoder, to respond slowly to 'out of sync' pulses. Else we can lose sync
 * with the bit stream.
 * 
 * See ibm_pc.c for technical details on the IBM-compatible MFM data format.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#define weak_sec(_type) (((_type) == TRKTYP_chaos_strikes_back_weak) ? 1 : 0)

static void *dungeon_master_weak_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->bytes_per_sector * ti->nr_sectors);
    unsigned int weak_sec = weak_sec(ti->type), nr_valid_blocks = 0;

    /* Fill value for all sectors seems to be 0xe5. */
    memset(block, 0xe5, ti->bytes_per_sector * ti->nr_sectors);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        int idx_off;
        uint8_t dat[2*514];
        uint16_t crc;
        unsigned int i;
        struct ibm_idam idam;

        /* IDAM */
        if ((idx_off = ibm_scan_idam(s, &idam)) < 0)
            continue;
        if ((idam.cyl != 0) || (idam.head != 1) ||
            (idam.no != 2) || (s->crc16_ccitt != 0))
            continue;

        idam.sec--;
        if ((idam.sec >= ti->nr_sectors) || is_valid_sector(ti, idam.sec))
            continue;

        /* DAM */
        if (ibm_scan_dam(s) < 0)
            continue;
        crc = s->crc16_ccitt;

        if (idam.sec == weak_sec) {
            /*
             * Weak-bit protection relies on authentic behaviour of FDC PLL
             * to respond slowly to marginal bits at edge of inspection window.
             */
            enum pll_mode old_mode = stream_pll_mode(s, PLL_authentic);
            if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
                break;
            stream_pll_mode(s, old_mode);
            mfm_decode_bytes(bc_mfm, 514, dat, dat);

            /*
             * Check each flakey byte is read as 0x68 or 0xE8. Rewrite as
             * originally mastered (always 0x68, with timing variation).
             */
            for (i = 20; i < 509; i++) {
                dat[i] &= 0x7f;
                if (dat[i] != 0x68)
                    break;
            }
            if (i != 509)
                continue;
            /* Re-compute the CRC on fixed-up data. */
            s->crc16_ccitt = crc16_ccitt(dat, 514, crc);
        } else {
            if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
                break;
            mfm_decode_bytes(bc_mfm, 514, dat, dat);
        }

        if (s->crc16_ccitt != 0)
            continue;

        memcpy(&block[idam.sec*512], dat, 512);
        set_sector_valid(ti, idam.sec);
        nr_valid_blocks++;
        if (idam.sec == 0)
            ti->data_bitoff = idx_off;
    }

    /* Must have found valid weak sector. */
    if (!is_valid_sector(ti, weak_sec)) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void dungeon_master_weak_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint8_t cyl = 0, hd = 1, no = 2;
    unsigned int sec, weak_sec = weak_sec(ti->type), i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        /* IDAM */
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895554);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, cyl);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, hd);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sec+1);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, no);
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 22; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);

        /* DAM */
        tbuf_start_crc(tbuf);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895545);
        if (sec == weak_sec) {
            uint16_t crc = crc16_ccitt(&dat[sec*512], 512, tbuf->crc16_ccitt);
            tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 32, &dat[sec*512]);
            /* Protection sector: randomise MSB of each byte in weak area. */
            for (i = 0; i < 512-64; i++)
                tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8,
                          (tbuf_rnd16(tbuf) & 1) ? 0x68 : 0xe8);
            tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 32, &dat[(sec+1)*512-32]);
            /* CRC is generated pre-randomisation. Restore it now. */
            tbuf->crc16_ccitt = crc;
        } else {
            tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 512, &dat[sec*512]);
        }
        tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
        for (i = 0; i < 40; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x4e);
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0x00);
    }
}

struct track_handler dungeon_master_weak_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_raw = dungeon_master_weak_write_raw,
    .read_raw = dungeon_master_weak_read_raw
};

struct track_handler chaos_strikes_back_weak_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_raw = dungeon_master_weak_write_raw,
    .read_raw = dungeon_master_weak_read_raw
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
