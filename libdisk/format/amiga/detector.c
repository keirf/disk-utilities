/*
 * disk/detector.c
 *
 * Custom format as used on Detector by Time Warp
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 dat[ti->len/4]
 *  u32 0x042A5448 :: SIG_1
 *  u32 0x92292292 :: SIG_2
 *  u16 crc16_ccitt
 * 
 * CRC is a duplicator's checksum: It is ignored by the game trackloader.
 *
 * TRKTYP_detector data layout:
 *  u8 sector_data[4096]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SIG_1 0x042A5448
#define SIG_2 0x92292292

static void *detector_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        stream_start_crc(s);

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        /* signature 1 */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != SIG_1)
            continue;

        /* signature 2 */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != SIG_2)
            continue;

        /* crc */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->crc16_ccitt != 0)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void detector_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_start_crc(tbuf);

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    /* data */
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

    /* signature 1 */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, SIG_1);

    /* signature 2 */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, SIG_2);

    /* crc */
    tbuf_emit_crc16_ccitt(tbuf, SPEED_AVG);
}

struct track_handler detector_handler = {
    .bytes_per_sector = 4096,
    .nr_sectors = 1,
    .write_raw = detector_write_raw,
    .read_raw = detector_read_raw
};

/*
 * The protection check reads the track 1.0 4 times and checks to see
 * if the first decoded u32 after the padding is different from the
 * previous track read.  If it is different then the protection 
 * passes. 
 * 
 * There are up to 3 chances for the values of the 4 read to be
 * different
 * 
 * Read 1 u32 is compared Read 2 u32
 *  - If different success
 * If previous check failed then Read 2 u32 is compared Read 3 u32
 *  - If different success
 * If previous check failed then Read 3 u32 is compared Read 4 u32 
 *  - If different success
 * 
 * If all 3 checks fail a Insert Disk 1 message appears briefly then
 * the game crashes
 * 
 *   
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u16 0x0404       - random between the 2 dumps but consitent for all 4 reads
 *  u16 weak bit
 *  u32 0x22952AA1   - random between the 2 dumps but consitent for all 4 reads
 *  u32 dat[40/4-1]  - Mutliple dumps had different data and only the first u32 is
 *                     used, which is set above. 
 * 
 * 
*/

static void *detector_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
    
        if (tracknr != 2)
            goto fail;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return memalloc(0);
    }

fail:
    return NULL;
}


static void detector_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x0404);
    tbuf_weak(tbuf, 8);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22952AA1);
    
    for (i = 1; i < 9; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
        tbuf_weak(tbuf, 8);
    }
}

struct track_handler detector_protection_handler = {
    .write_raw = detector_protection_write_raw,
    .read_raw = detector_protection_read_raw
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
