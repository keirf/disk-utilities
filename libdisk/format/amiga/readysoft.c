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
    unsigned int nr_valid_blocks = 0, least_block = ~0u;

    block = memalloc(ti->nr_sectors*ti->bytes_per_sector);

    ti->data_bitoff = s->index_offset_bc - 47;

    /* decode sector data */
    while ((stream_next_bit(s) != -1) &&
        (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t raw[2], hdr, hdrchk, dat[2*ti->bytes_per_sector/4];
        uint32_t csum, sum, sig, trk, disknr;
        unsigned int sec, i, bitoff;

        /* sync */
        if ((uint16_t)s->word != 0x4489)
            continue;

        /* sync */
        if (stream_next_bits(s, 32) == -1)
            break;
        if (s->word != 0x44894489)
            continue;

        bitoff = s->index_offset_bc - 47;

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

        if (least_block > sec) {
            ti->data_bitoff = bitoff;
            least_block = sec;
        }
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
 * Custom format as used by Cosmic Bouncer
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u16 0x5555 :: Padding
 *  u32 track number
 *  u32 data[6600]
 *  u32 checksum 1 :: Sum of decoded data
 *  u32 checksum 2 :: Eor'd over decoded data
 *

 *
 * TRKTYP_cosmic_bouncer data layout:
 *  u8 sector_data[6600]
 */

static void *cosmic_bouncer_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], dat[ti->len/4];
        uint32_t sum1, sum2, trk, chk1, chk2;
        unsigned int i, track_len;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* header */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);
        if (be32toh(trk) != tracknr)
            continue;

        /* data */
        track_len = ti->len/4;
        if (tracknr == 52)
            track_len = 1550;
        for (i = sum1 = sum2 = 0; i < track_len; i++){
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum1 += be32toh(dat[i]);
            sum2 ^= be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &chk1);

        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &chk2);

        if (sum1 != be32toh(chk1))
            continue;

        if (sum2 != be32toh(chk2))
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void cosmic_bouncer_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum1, sum2;
    unsigned int i, track_len;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);
    /* track number */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, tracknr);

    /* data */
    track_len = ti->len/4;
    if (tracknr == 52)
        track_len = 1550;
    for (i = sum1 = sum2 = 0; i < track_len; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum1 += be32toh(dat[i]);
        sum2 ^= be32toh(dat[i]);
    }

    /* checksum 1 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum1);

    /* checksum 2 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum2);

    if (tracknr == 52)
        for (i = 0; i < 98; i++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
        }
}

struct track_handler cosmic_bouncer_handler = {
    .bytes_per_sector = 6600,
    .nr_sectors = 1,
    .write_raw = cosmic_bouncer_write_raw,
    .read_raw = cosmic_bouncer_read_raw
};

/*
 * Custom format as used on The C64 Emulator by Readysoft.
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 Sync (multiple see sync array) 
 *  u32 0x55555555
 *  u32 0x55555555
 *  u32 0x55555555
 *  u32 0x55555555
 *  u32 dat[ti->len/4]
 * 
 * Checksum decoded as part of the data dat[ti->len/4-3]
 * The lower word of the checksum is calculated by EORing of all words (u16)
 * The upper word of the checksum is calculated by adding all words (u16)
 *
 * The last 2 u32's are not part of the checksum
 * 
 * TRKTYP_c64 data layout:
 *  u8 sector_data[6592]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>


static const uint16_t syncs[];

static void *c64_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], pad;
        uint16_t sum1, sum2;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != syncs[tracknr])
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 4, raw, &pad);
        if (pad != 0xffffffff)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 4, raw, &pad);
        if (pad != 0xffffffff)
            continue;

        for (i = sum1 = sum2 = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);

            if(i < 1645) {
                sum1 ^= (uint16_t)(be32toh(dat[i]) >> 16);
                sum1 ^= (uint16_t)(be32toh(dat[i]));
                sum2 += (uint16_t)(be32toh(dat[i]) >> 16);
                sum2 += (uint16_t)(be32toh(dat[i]));
            }
        }
        if (sum2 != (uint16_t)(be32toh(dat[ti->len/4-3]) >> 16) &&
            sum1 != (uint16_t)(be32toh(dat[ti->len/4-3])))
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

static void c64_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    uint16_t sum1, sum2;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, syncs[tracknr]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0xffffffff);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0xffffffff);

    for (i = sum1 = sum2 = 0; i < ti->len/4; i++) {
        if(i < 1645) {
            sum1 ^= (uint16_t)(be32toh(dat[i]) >> 16);
            sum1 ^= (uint16_t)(be32toh(dat[i]));
            sum2 += (uint16_t)(be32toh(dat[i]) >> 16);
            sum2 += (uint16_t)(be32toh(dat[i]));
        }
    }
    dat[ti->len/4-3] = be32toh((sum2 << 16) | sum1);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
    }
}

struct track_handler c64_emulator_handler = {
    .bytes_per_sector = 6592,
    .nr_sectors = 1,
    .write_raw = c64_write_raw,
    .read_raw = c64_read_raw
};

static const uint16_t syncs[] = {
    0x0000, //0
    0x5ADA, //1
    0x591B, //2
    0x591B, //3
    0x5563, //4
    0x5563, //5
    0x5563, //6
    0x1962, //7
    0x5534, //8
    0x1962, //9
    0x4B24, //10
    0x4D1A, //11
    0x4A6A, //12
    0x2D65, //13
    0x4A6A, //14
    0x3592, //15
    0x1235, //16
    0x2345, //17
    0x1352, //18
    0x5356, //19
    0x5356, //20
    0x651A, //21
    0x5935, //22
    0x2D55, //23
    0x5935, //24
    0x3592, //25
    0x2D2B, //26
    0x2D2B, //27
    0x31A3, //28
    0x651A, //29
    0x52D6, //30
    0x695A, //31
    0x1A32, //32
    0x1A32, //33
    0x6962, //34
    0x5ADA, //35
    0x591B, //36
    0x6962, //37
    0x5ADA, //38
    0x5ADA, //39
    0x5ADA, //40
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
