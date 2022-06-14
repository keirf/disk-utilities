/*
 * disk/readysoft.c
 *
 * Custom format as used by ReadySoft for the following games:
 * 
 * Dragon's Lair II: Time Warp
 * Dragon's Lair III: The Curse Of Mordread
 * Guy Spy
 * Space Ace
 * Space Ace II
 * Wrath of the Demon
 *
 * Written in 2022 by Keith Krellwitz
 * 
 * 4 sectors with a length of 1600 bytes
 *
 * RAW TRACK SECTOR LAYOUT:
 *  u16 0x4489, 0x4489 0x4489 :: Sync
 *  u32 Header :: 0xFF0#0### - FF0 disk number + 0 + sector + track number
 *  u32 Header Checksum :: 0xFFFFFFFF - Header
 *  u32 data[4][1600]
 *  u32 Data Checksum
 *  u32 Sig :: 0x53444446 - SDDF
 *  u16 6x 0xaaaa :: Padding
 * 
 *  Checksum is the sum of all decoded data for each sector
 * 
 * Note: The padding is not checked by Wrath of the Demon, but Space Ace
 * validates the padding.
 * 
 * TRKTYP_readysoft data layout:
 *  u8 sector_data[4*1600]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SIG_SDDF 0x53444446

static void *readysoft_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    char *block;
    unsigned int nr_valid_blocks = 0;

    block = memalloc(ti->nr_sectors*ti->bytes_per_sector);

    ti->data_bitoff = s->index_offset_bc - 47;

    /* decode sector data */
    while ((stream_next_bit(s) != -1) &&
        (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t raw[2], hdr, hdrchk, dat[ti->bytes_per_sector];
        uint32_t csum, sum, sig, trk, disknr;
        unsigned int sec, i;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;

        /* sync */
        if (stream_next_bits(s, 32) == -1)
            break;
        if (s->word != 0x44894489)
            continue;

        /* header */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;        
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        /* header checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;        
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdrchk);
        
        if ((hdrchk ^ hdr) + 1 != 0)
            continue;

        /* store disk number */
        disknr = (uint8_t)(be32toh(hdr) >> 16);
        if (disktag == NULL) {
            disktag = (struct disktag_disk_nr *)
                    disk_set_tag(d, DSKTAG_disk_nr, 4, &disknr);
        }

        /* extract sector and verify it has not already been added */
        sec = (uint8_t)(be32toh(hdr) >> 8) ;
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        trk =(uint8_t)be32toh(hdr);
        if (trk != tracknr)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->bytes_per_sector) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, dat, dat);

        /* calculate checksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++) {
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            break;        
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* validate sector checksum. */
        if (be32toh(csum) != sum)
            continue;

        /* SIG */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;        
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);

        if (be32toh(sig) != SIG_SDDF)
            continue;

        memcpy(&block[sec*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
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

static void readysoft_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    uint32_t dat[ti->bytes_per_sector/4], hdr;
    uint32_t csum;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        /* sync*/
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

        memcpy(dat, &ti->dat[sec*ti->bytes_per_sector], ti->bytes_per_sector);

        /* calculate checksum */
        for(i = csum = 0; i < ti->bytes_per_sector/4; i++) {
            csum += be32toh(dat[i]);
        }

        /* header */
        hdr = 0xff000000 | disktag->disk_nr << 16 | (uint16_t)sec << 8 | tracknr;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

         /* header checksum*/
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0xffffffff-hdr);
   
        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector, dat);

        /* checksum */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

        /* sig */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, SIG_SDDF);

        /* padding */
        for (i = 0; i <= 5; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xaaaa);
    }
}

struct track_handler readysoft_handler = {
    .bytes_per_sector = 1600,
    .nr_sectors = 4,
    .write_raw = readysoft_write_raw,
    .read_raw = readysoft_read_raw
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
