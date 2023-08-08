/*
 * disk/elfmania.c
 *
 * Custom format as used on Elfmania by Renegade
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 * 23 back to back sectors
 *  u32 0x44894489 :: Sync
 *  u32 header :: 0xDDDDTTSS 
 *                D = Disk Number
 *                T = Track Number
 *                S = Sector Number
 *  u32 header checksum :: !(header ^ header checksum) == 0
 *  u32 data checksum
 *  u32 dat[256/4]
 * 
 * After the 23 sector on disk 1 - same on all tracks 
 *  u32 dat[16]
 *  u32 checksum
 * 
 * TRKTYP_elfmania data layout:
 *  u8 sector_data[23*256]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *elfmania_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
   struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int nr_valid_blocks = 0, least_block = ~0u, sec, i;
    uint8_t disknr;
    
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);

    block = memalloc(ti->nr_sectors*(ti->bytes_per_sector+4+20));

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        uint32_t dat[2*ti->bytes_per_sector/4+1], sum;
        uint32_t raw[2], hdr, hdrchk, csum, bitoff;

        /* sync */
        if (s->word != 0x44894489)
            continue;

        bitoff = s->index_offset_bc - 31;

        /* header disk, track and sector */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        /* header checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdrchk);

        /* data checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (0xffffffff - (be32toh(hdr)^be32toh(hdrchk)) != 0)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->bytes_per_sector) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, dat, &dat);
        dat[ti->bytes_per_sector/4] = be32toh(hdrchk);

        /* track number check */
        if ((uint8_t)(be32toh(hdr)>>8) != tracknr)
            continue;
        /* get sector number */
        sec = (uint8_t)be32toh(hdr);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* store disk number */
        disknr = (uint8_t)(be32toh(hdr) >> 16);
        if (disktag == NULL) {
            disktag = (struct disktag_disk_nr *)
                    disk_set_tag(d, DSKTAG_disk_nr, 4, &disknr);
        }
        /* checksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++)
            sum += be32toh(dat[i]);
        if ((0xffffffff^sum) != be32toh(csum))
            continue;

        memcpy(&block[sec*(ti->bytes_per_sector+4)], &dat, ti->bytes_per_sector+4);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;

        if (least_block > sec) {
            ti->data_bitoff = bitoff;
            least_block = sec;
        }
    }

    /* Disk one has extra data that is used to identify disk 1 on each track
       after sector 0x16. Is seems to be some sort of protection as the header 
       of the track already contains the disk number */
    if (disknr == 1)
        while (stream_next_bit(s) != -1) {
            uint32_t raw[2], dat[5], sum;

            /* locate the start of the extra data */
            if (s->word != 0xAAA52552)
                continue;
            raw[0] = be32toh(s->word);
            if (stream_next_bits(s, 32) == -1)
                break;
            raw[1] = be32toh(s->word);
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[1]);
                    
            if (be32toh(dat[1]) != 0x005B1BE0)
                continue;

            /* decode etxra track data */
            dat[0] = 0;
            for (i = 2; i < 5; i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    break;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            }

            /* data checksum */
            for (i = sum = 0; i < 4; i++) {
                sum += be32toh(dat[i]);
            }
            sum ^= be32toh(dat[4]);

            if (0xffffffff-sum != 0)
                nr_valid_blocks = 0;

            memcpy(&block[23*(ti->bytes_per_sector+4)], &dat, 20);
            break;
        }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    ti->total_bits = 106000;
    return block;
}

static void elfmania_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    uint32_t dat[ti->bytes_per_sector/4+1], sum, hdr;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        memcpy(dat, &ti->dat[sec*(ti->bytes_per_sector+4)], ti->bytes_per_sector+4);

        /* calculate checksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++) {
            sum += be32toh(dat[i]);
        }

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

        /* header disk, track and sector */
        hdr = 0x00000000 | (uint8_t)disktag->disk_nr << 16 | tracknr << 8 | (uint16_t)sec;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
    
        /* header checksum */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, dat[ti->bytes_per_sector/4]);

        /* checksum */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0xffffffff^sum);

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector, dat);
    }

    /* If disk 1 then write the extra data that is after the last sector. Possibly used
       as protection as the sector header contains the disk number */
    if ((uint8_t)disktag->disk_nr == 1) {
        memcpy(dat, &ti->dat[23*(ti->bytes_per_sector+4)], 20);
        for (i = 0; i < 5; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler elfmania_handler = {
    .bytes_per_sector = 256,
    .nr_sectors = 23,
    .write_raw = elfmania_write_raw,
    .read_raw = elfmania_read_raw
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
