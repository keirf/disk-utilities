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
#include "private.h"

#include <arpa/inet.h>

#define weak_sec(_type) (((_type) == TRKTYP_chaos_strikes_back_weak) ? 1 : 0)

static void *dungeon_master_weak_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->bytes_per_sector * ti->nr_sectors);
    unsigned int weak_sec = weak_sec(ti->type), valid_blocks = 0;

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

        if (sec == weak_sec) {
            /*
             * Weak-bit protection relies on authentic behaviour of FDC PLL
             * to respond slowly to marginal bits at edge of inspection window.
             */
            enum pll_mode old_mode = stream_pll_mode(s, PLL_authentic);
            if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
                break;
            stream_pll_mode(s, old_mode);
            mfm_decode_bytes(MFM_all, 514, dat, dat);

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
            mfm_decode_bytes(MFM_all, 514, dat, dat);
        }

        if (s->crc16_ccitt != 0)
            continue;

        memcpy(&block[sec*512], dat, 512);
        valid_blocks |= 1u << sec;
        if (sec == 0)
            ti->data_bitoff = idx_off;
    }

    /* Must have found valid weak sector. */
    if (!(valid_blocks & (1u<<weak_sec))) {
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
    unsigned int sec, weak_sec = weak_sec(ti->type), i;

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
        if (sec == weak_sec) {
            uint16_t crc = crc16_ccitt(&dat[sec*512], 512, tbuf->crc16_ccitt);
            static unsigned int seed = 0;
            tbuf_bytes(tbuf, SPEED_AVG, MFM_all, 32, &dat[sec*512]);
            /* Protection sector: randomise MSB of each byte in weak area. */
            for (i = 0; i < 512-64; i++)
                tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8,
                          (rand_r(&seed) & 1) ? 0x68 : 0xe8);
            tbuf_bytes(tbuf, SPEED_AVG, MFM_all, 32, &dat[(sec+1)*512-32]);
            /* CRC is generated pre-randomisation. Restore it now. */
            tbuf->crc16_ccitt = crc;
        } else {
            tbuf_bytes(tbuf, SPEED_AVG, MFM_all, 512, &dat[sec*512]);
        }
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

struct track_handler chaos_strikes_back_weak_handler = {
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
