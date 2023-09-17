/*
 * disk/no_second_prize.c
 *
 * Custom format as used on No Second Prize by Thalion
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 0x44894489 :: Sync
 *  u16 0x2aaa :: padding
 *  u8 dat[4610]
 *
 * The checksum uses a lookup table and a seed 
 * of 0xcdb4 over the decoded data.
 *
 * TRKTYP_no_second_prize data layout:
 *  u8 sector_data[4610]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint16_t lookup_table[];

static void *no_second_prize_save_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint8_t raw[2], dat[ti->len];
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 63;

        /* sync */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44894489)
            continue;

        /* padding */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x2aaa)
            continue;

        /* data */
        for (i = 0; i < ti->len; i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm, 1, raw, &dat[i]);
        }

        /* data checksum */
        uint16_t d0 = 0xcdb4, d1=0;
        for (i = 0; i < ti->len; i++) {
            d0 = (uint8_t)d0 << 8 | (uint8_t)(d0 >> 8);
            d1 = dat[i];
            d1 ^= (uint8_t)d0;
            d0 ^= (uint8_t)d0;
            d1 = lookup_table[d1];
            d0 ^= d1;
        }

        if (d0 != 0 || d1 != 0)
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

static void no_second_prize_save_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);
    /* data */
    for (i = sum = 0; i < ti->len; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
    }
}

struct track_handler no_second_prize_save_handler = {
    .bytes_per_sector = 4610,
    .nr_sectors = 1,
    .write_raw = no_second_prize_save_write_raw,
    .read_raw = no_second_prize_save_read_raw
};


/*
 * The protection track for No Second Prize
 *
 * RAW TRACK LAYOUT: 
 * 140 consecutive sectors with no padding inbetween
 *  u32 0x55551225  or 0xaaa91224 :: Sync shifted until 4489 
 *      to get the data shift value
 *   u8 8x 0xff or 0x0 :: padding (depends on even and odd sector numbers)
 *   u8 0x12 
 *   u8 0x34
 *   u8 0x00 or on the last sector 0x3f
 *   u8 sector number or on the last sector 0xff
 *   u8 32x 0xff or 0x00 (depends on even and odd sector numbers).  The 
 *      last sector will be 0xff
 *
 * TRKTYP_no_second_prize data layout:
 *  u8 sector_data[4610]
 *  
 */

#define SECTOR_OFFSET 11

static uint32_t rol(uint32_t n, uint32_t value){
  return (value << n) | (value >> (32-n));
} 

static unsigned int get_data_shift(uint32_t value){
    unsigned int i, offset = 0;
    uint32_t temp = (((uint16_t)value) << 16) | (uint16_t)(value >> 16);
    for (i = 0; i < 15; i++) {
        offset = i;
        if ((uint16_t)temp == 0x4489)
            break;
        temp = rol(1, temp);
    }
    return offset;
}

static void *no_second_prize_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int nr_valid_blocks = 0;
    unsigned int i, offset=0, sec;
    char *block;
    
    block = memalloc(ti->len);

    /* get offset */
    while (stream_next_bit(s) != -1) {

        if (s->word != 0x55551225 && s->word != 0xaaa91224)
            continue;    
        offset = get_data_shift(s->word);
        if (offset == 15)
            continue;

        break;
    }

    stream_reset(s);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {
        uint8_t dat[ti->bytes_per_sector];
        uint16_t raw_dat[ti->bytes_per_sector+1];
        uint32_t temp;
        
        temp = (((uint16_t)s->word) << 16) | (uint16_t)(s->word >> 16);
        temp = rol(offset, temp);

        /* sync */
        if ((uint16_t)temp != 0x4489)
            continue;

        raw_dat[0] = (uint16_t)s->word;
        for (i = 1; i < ti->bytes_per_sector; i++) {
            if (stream_next_bits(s, 16) == -1)
                break;
            raw_dat[i] = s->word;
        }
        raw_dat[ti->bytes_per_sector] = raw_dat[i];

        for (i = 0; i < ti->bytes_per_sector-1; i++) {
            temp = (raw_dat[i] << 16) | raw_dat[i+1];
            temp <<= offset;
            dat[i] = mfm_decode_word(temp >> 16);
        }

        sec = dat[SECTOR_OFFSET];
        if (sec == 0xff)
            sec = 139;
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        set_sector_valid(ti, sec);
        memcpy(&block[sec*(ti->bytes_per_sector-1)], &dat, ti->bytes_per_sector-1);
        nr_valid_blocks++;
    }
    stream_next_index(s);
    ti->total_bits = (s->track_len_bc/100)*100+100;
    ti->data_bitoff = 0;

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }

    return block;
}

static void no_second_prize_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
        for(i = 0; i < ti->bytes_per_sector-1; i++) {
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[sec*(ti->bytes_per_sector-1)+i]);
        }
    }
}

struct track_handler no_second_prize_protection_handler = {
    .bytes_per_sector = 45,
    .nr_sectors = 140,
    .write_raw = no_second_prize_protection_write_raw,
    .read_raw = no_second_prize_protection_read_raw
};

static const uint16_t lookup_table [] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
    0xAAAA, 0xAAA9, 0xAAA4, 0xAAA5, 0xAA92, 0xAA91, 0xAA94, 0xAA95,
    0xAA4A, 0xAA49, 0xAA44, 0xAA45, 0xAA52, 0xAA51, 0xAA54, 0xAA55,
    0xA92A, 0xA929, 0xA924, 0xA925, 0xA912, 0xA911, 0xA914, 0xA915,
    0xA94A, 0xA949, 0xA944, 0xA945, 0xA952, 0xA951, 0xA954, 0xA955,
    0xA4AA, 0xA4A9, 0xA4A4, 0xA4A5, 0xA492, 0xA491, 0xA494, 0xA495,
    0xA44A, 0xA449, 0xA444, 0xA445, 0xA452, 0xA451, 0xA454, 0xA455,
    0xA52A, 0xA529, 0xA524, 0xA525, 0xA512, 0xA511, 0xA514, 0xA515,
    0xA54A, 0xA549, 0xA544, 0xA545, 0xA552, 0xA551, 0xA554, 0xA555,
    0x92AA, 0x92A9, 0x92A4, 0x92A5, 0x9292, 0x9291, 0x9294, 0x9295,
    0x924A, 0x9249, 0x9244, 0x9245, 0x9252, 0x9251, 0x9254, 0x9255,
    0x912A, 0x9129, 0x9124, 0x9125, 0x9112, 0x9111, 0x9114, 0x9115,
    0x914A, 0x9149, 0x9144, 0x9145, 0x9152, 0x9151, 0x9154, 0x9155,
    0x94AA, 0x94A9, 0x94A4, 0x94A5, 0x9492, 0x9491, 0x9494, 0x9495,
    0x944A, 0x9449, 0x9444, 0x9445, 0x9452, 0x9451, 0x9454, 0x9455,
    0x952A, 0x9529, 0x9524, 0x9525, 0x9512, 0x9511, 0x9514, 0x9515,
    0x954A, 0x9549, 0x9544, 0x9545, 0x9552, 0x9551, 0x9554, 0x9555,
    0x4AAA, 0x4AA9, 0x4AA4, 0x4AA5, 0x4A92, 0x4A91, 0x4A94, 0x4A95,
    0x4A4A, 0x4A49, 0x4A44, 0x4A45, 0x4A52, 0x4A51, 0x4A54, 0x4A55,
    0x492A, 0x4929, 0x4924, 0x4925, 0x4912, 0x4911, 0x4914, 0x4915,
    0x494A, 0x4949, 0x4944, 0x4945, 0x4952, 0x4951, 0x4954, 0x4955,
    0x44AA, 0x44A9, 0x44A4, 0x44A5, 0x4492, 0x4491, 0x4494, 0x4495,
    0x444A, 0x4449, 0x4444, 0x4445, 0x4452, 0x4451, 0x4454, 0x4455,
    0x452A, 0x4529, 0x4524, 0x4525, 0x4512, 0x4511, 0x4514, 0x4515,
    0x454A, 0x4549, 0x4544, 0x4545, 0x4552, 0x4551, 0x4554, 0x4555,
    0x52AA, 0x52A9, 0x52A4, 0x52A5, 0x5292, 0x5291, 0x5294, 0x5295,
    0x524A, 0x5249, 0x5244, 0x5245, 0x5252, 0x5251, 0x5254, 0x5255,
    0x512A, 0x5129, 0x5124, 0x5125, 0x5112, 0x5111, 0x5114, 0x5115,
    0x514A, 0x5149, 0x5144, 0x5145, 0x5152, 0x5151, 0x5154, 0x5155,
    0x54AA, 0x54A9, 0x54A4, 0x54A5, 0x5492, 0x5491, 0x5494, 0x5495,
    0x544A, 0x5449, 0x5444, 0x5445, 0x5452, 0x5451, 0x5454, 0x5455,
    0x552A, 0x5529, 0x5524, 0x5525, 0x5512, 0x5511, 0x5514, 0x5515,
    0x554A, 0x5549, 0x5544, 0x5545, 0x5552, 0x5551, 0x5554, 0x5555
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
