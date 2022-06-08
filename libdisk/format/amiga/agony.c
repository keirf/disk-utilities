/*
 * disk/agony.c
 *
 * Custom format as used by Agony from Psygnosis:
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x45224522 :: Sync
 *  u32 0x51225122 :: Sync 2
 *  u32 0x22912291 :: Padding
 *  u16 0x2891 :: Padding
 *  u32 0x51225122 :: Sync before each sector
 *  u32 Checksum and Sector ::  Checksum is the lower word and the sector is the upper word
 *  u32 data[12][512]
 *  u32 Padding between sectors
 * 
 *  Checksum is the sum of all decoded words
 * 
 * 
 * TRKTYP_agony data layout:
 *  u8 sector_data[12*512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *agony_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int nr_valid_blocks = 0;

    block = memalloc((ti->nr_sectors*(ti->bytes_per_sector+4)) );

    /* check for first sync */
    while (stream_next_bit(s) != -1) {

        /* sync */
        if (s->word == 0x45224522)
            break;
    }

    ti->data_bitoff = s->index_offset_bc - 31;

    /* decode sector data */
    while ((stream_next_bit(s) != -1) &&
        (nr_valid_blocks != ti->nr_sectors)) {

        uint16_t csum, sum;
        uint32_t raw[2], hdr, dat[(ti->bytes_per_sector)/4+1];
        unsigned int sec, i;

        /* sync 2 */
        if (s->word != 0x51225122)
            continue;

        /* checksum and sector */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;        
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        csum = (uint16_t)be32toh(hdr);

        /* extract sector and verify it has not already been added */
        sec = (uint16_t)(be32toh(hdr) >> 16) - 0xff31;
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* read and decode data. */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += (uint16_t)be32toh(dat[i]) + (uint16_t)(be32toh(dat[i]) >> 16);
        }

        /* padding value never checked - skipped adda.l #$00000004,a1*/
        if (stream_next_bits(s, 32) == -1)
            break;
        dat[ti->bytes_per_sector/4] = s->word;

        /* validate the checksum. */
        if (csum != sum)
            continue;

        memcpy(&block[sec*(ti->bytes_per_sector+4)], &dat, (ti->bytes_per_sector+4));
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
    }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    stream_next_index(s);
    ti->total_bits = s->track_len_bc;
    return block;
}

static void agony_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector/4], hdr;
    uint16_t csum;
    unsigned int sec, i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x45224522);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x51225122);

    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22912291);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2891);

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync 2 before each sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x51225122);

        memcpy(dat, &ti->dat[sec*(ti->bytes_per_sector+4)], ti->bytes_per_sector+4);

        /* calculate checksum */
        for(i = csum = 0; i < (ti->bytes_per_sector)/4; i++) {
            csum += (uint16_t)be32toh(dat[i]) + (uint16_t)(be32toh(dat[i]) >> 16);
        }

        /* sector and checksum */
        hdr = (sec + 0xff31) << 16 | csum;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
    
        /* data */
        for (i = 0; i <  (ti->bytes_per_sector)/4; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

        /* padding */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->bytes_per_sector/4]);
    }
}

struct track_handler agony_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = agony_write_raw,
    .read_raw = agony_read_raw
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
