/*
 * disk/jupiters_masterdrive.c
 *
 * Custom format as used by Jupiter's Masterdrive from Ubi Soft:
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 23 sectors:
 *  u32 0x44894489 :: Sync
 *  u16 pad :: 0x2aaa
 *  u16 pad :: 0xa888
 *  u32  data[260] :: Even blocks (252 + 8 bytes for headder and checksum)
 *  u32  data[260] :: Odd blocks (252 + 8 bytes for headder and checksum)
 *  u8 gap[5]
 * 
 *  Header is (tracknr/2) << 24 | (sec*4 << 8);
 * 
 *  The header and checksum are part of the data dat[0] and dat[1]
 *  Add 8 to the bytes_per_sector to account for the 
 *  header and checksum for each sector
 *
 * TRKTYP_jupiters_masterdrive data layout:
 *  u8 sector_data[23*252]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *jupiters_masterdrive_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int nr_valid_blocks = 0;

    block = memalloc((ti->nr_sectors*(ti->bytes_per_sector+8)) );

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t csum;
        uint32_t raw2[2*(ti->bytes_per_sector+8)/4];
        uint32_t dat[(ti->bytes_per_sector+8)/4];
        unsigned int sec, i;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
             break;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0xa888)
            continue;

        /* Read and decode data. */
        if (stream_next_bytes(s, raw2, 2*(ti->bytes_per_sector+8)) == -1)
           break;
        mfm_decode_bytes(bc_mfm_even_odd, (ti->bytes_per_sector+8), raw2, &dat);

        sec = ((uint16_t)dat[0] >> 8) / 4;
        
        /* calucalte checksum */
        csum = 0;
        for(i = 2; i < (ti->bytes_per_sector+8)/4; i++) {
            csum += be32toh(dat[i]);
        }

        /* Validate the checksum. */
        if (csum != be32toh(dat[1]))
            continue;

        memcpy(&block[sec*(ti->bytes_per_sector)], &dat[2], (ti->bytes_per_sector));
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
    }
    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void jupiters_masterdrive_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector+8], csum, hdr;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa888);

        /* calculate header */
        hdr = (tracknr/2) << 24 | (sec*4 << 8);
        dat[0] = hdr;
        memcpy(&dat[2], &ti->dat[sec*ti->bytes_per_sector], ti->bytes_per_sector);

        /* calculate checksum */
        csum = 0;
        for(i = 2; i < (ti->bytes_per_sector+8)/4; i++) {
            csum += be32toh(dat[i]);
        }
        dat[1] = htobe32(csum);

        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, (ti->bytes_per_sector+8), dat);

        for (i = 0; i < 5; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    }
}

struct track_handler jupiters_masterdrive_handler = {
    .bytes_per_sector = 252,
    .nr_sectors = 23,
    .write_raw = jupiters_masterdrive_write_raw,
    .read_raw = jupiters_masterdrive_read_raw
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
