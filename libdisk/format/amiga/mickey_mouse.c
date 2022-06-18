/*
 * disk/mickey_mouse.c
 * 
 * Custom format as used in Mickey Mouse by Gremlin.
 * 
 * Written in 2022 by Keith Krellwitz.  This is based on
 * Keir Fraser's Federation Of Free Traders decoder.
 * 
 * RAW TRACK LAYOUT:
 *  3 back-to-back sectors with u32 gap.
 *  Total encoded sector size, including gap, is 0xfb2 (4018) bytes.
 * RAW SECTOR:
 *  u32 0xa245a245, 0x8a918a91/0x8a914a91, 0xa291a291 :: sync marks
 *      each sector has a different sync
 *  u8 0xff
 *  u8 tracknr + (tracknr % 2 == 0 ? 1 : -1)
 *  u8 sec
 *  u8 checksum upper byte
 *  u8 checksum lower byte
 *  u8 data[2000]
 *  u32 gap
 * 
 * Sector   Sync
 * 0        0xa245a245
 * 1        0x8a918a91 or 0x8a914a91
 * 2        0xa291a291
 * 
 * Note: When debugging on the Amiga the sector 1 sync is always
 * 0x8a918a91, but when using disk-analysis it shows up as 0x8a918a91
 * or 0x8a914a91. Added 12 bytes to block size and add the sync
 * for each sector to the data.
 *  
 * MFM encoding:
 *  No even/odd split
 * 
 * Checksum is the sum of decoded words
 * 
 * TRKTYP_mickey_mouse data layout:
 *  u8 sector_data[3][2000]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *mickey_mouse_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->len+12);
    unsigned int i, nr_valid_blocks = 0, least_block = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t idx_off = s->index_offset_bc - 31, sync;
        uint16_t csum, sum;
        uint8_t sec, *p, chkff, trk, chk1, chk2;

        if (s->word != 0xa291a291 && s->word != 0x8a918a91 && \
            s->word != 0x8a914a91 && s->word != 0xa245a245)
            continue;
        sync = s->word;
 
        /* 0xff */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        chkff = mfm_decode_word((uint16_t)s->word);
        if (chkff != 0xff)
            continue;

        /* track number */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        trk = mfm_decode_word((uint16_t)s->word);
        if (trk != tracknr + (tracknr % 2 == 0 ? 1 : -1))
            continue;

        /* sector */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        sec = mfm_decode_word((uint16_t)s->word);
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* high byte of the checksum */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        chk1 = mfm_decode_word((uint16_t)s->word);

        /* low byte of the checksum */
        if (stream_next_bits(s, 16) == -1)
            goto done;
        chk2 = mfm_decode_word((uint16_t)s->word);

        /* data */
        p = &block[sec * (ti->bytes_per_sector + 4)];
        for (i = 0; i < ti->bytes_per_sector; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto done;
            p[i] = mfm_decode_word((uint16_t)s->word);
        }

        /* store sector sync at the end of the data */
        p[ti->bytes_per_sector] = (uint8_t)(sync >> 24);
        p[ti->bytes_per_sector + 1] = (uint8_t)(sync >> 16);
        p[ti->bytes_per_sector + 2] = (uint8_t)(sync >> 8);
        p[ti->bytes_per_sector + 3] = (uint8_t)(sync);

        /* data checksum calculation */
        sum = 0;
        for (i = 0; i < ti->bytes_per_sector; i+=2)
            sum += ((uint16_t)p[i] << 8 | p[i+1]);

        csum = (uint16_t)chk1 << 8 | chk2;
        if (csum != sum)
            continue;

        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        if (least_block > sec) {
            ti->data_bitoff = idx_off;
            least_block = sec;
        }
    }
done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    for (i = 0; i < ti->nr_sectors; i++)
        if (is_valid_sector(ti, i))
            break;
    ti->data_bitoff -= i * 0xfb2;
    ti->total_bits = s->track_len_bc;
    return block;
}

static void mickey_mouse_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    uint16_t sum;
    unsigned int i, j;
    uint32_t sync;
    for (i = 0; i < ti->nr_sectors; i++) {

        /* extract sync from data */
        sync = dat[ti->bytes_per_sector] << 24;
        sync |= dat[ti->bytes_per_sector + 1] << 16;
        sync |= dat[ti->bytes_per_sector + 2] << 8;
        sync |= dat[ti->bytes_per_sector + 3];
        
        /* header */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, sync);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr + (tracknr % 2 == 0 ? 1 : -1));
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, i);

        /* csum */
        for (j = sum = 0; j < ti->bytes_per_sector; j+=2)
            sum += ((uint16_t)dat[j] << 8 | dat[j+1]);

        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sum >> 8);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, (uint8_t)sum);

        /* data */
        for (j = 0; j < ti->bytes_per_sector; j++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[j]);
        }

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

        dat += ti->bytes_per_sector + 4;
    }
}

struct track_handler mickey_mouse_handler = {
    .bytes_per_sector = 2000,
    .nr_sectors = 3,
    .write_raw = mickey_mouse_write_raw,
    .read_raw = mickey_mouse_read_raw
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
