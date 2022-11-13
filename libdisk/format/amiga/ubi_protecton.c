/*
 * disk/ubi_protection.c
 *
 * Protection definition for the following games:
 * 
 * Iron Lord                - 0x552aa549 - UBI 		    - 1989
 * Puffy's Saga             - 0x552aa549 - UBI 		    - 1989
 * Cosmo Ranger             - 0x5524a529 - Turtle Byte	- 1989
 * Minos                    - 0x5524a529 - Turtle Byte	- 1990
 * Takado                   - 0x5524a529 - King Soft	- 1988
 * Scorpio                  - 0x5524a529 - King Soft	- 1988
 * Aunt Arctic Adventure    - 0x5524a529 - Mindware	    - 1989
 * Limes & Napoleon         - 0x552aa549 - EAS		    - 1989
 * Fred                     - 0x552aa549 - UBI		    - 1989
 * RanX                     - 0x552aa549 - UBI		    - 1990
 * Zombi                    - 0x552aa549 - UBI		    - 1989
 * Final Command            - 0x552aa549 - UBI		    - 1989
 * My Funny Maze            - 0x5524a529 - Turtle Byte	- 1989
 * Vindex                   - 0x5524a529 - Turtle Byte	- 1989
 * 
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * 
 * Puffy's Saga protection check occurs after pressing fire to enter level 3
 * 
 * Aunt Arctic Adventure protection check occurs before entering level 6
 * 
 * All other games check at some point before you start the game, usually just
 * before or just after the title screen.
 * 
 * UBI Protection Validation
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 Signature (0x5524a529 or 0x552aa549)
 *  u32 Protection Counter Value
 * 
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct ubi_protecton_info {
    uint32_t sig;
};

static void *ubi_protecton_validation_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ubi_protecton_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], count, *data;

        // sync
        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        // signature
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != info->sig)
            continue;

        // data
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &count);

        stream_next_index(s);
        ti->total_bits = (101246) & ~31;
        ti->data_bitoff = 0;
        data = memalloc(4);
        *data = count;
        set_all_sectors_valid(ti);
        return data;
    }
fail:
    return NULL;
}

static void ubi_protecton_validation_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ubi_protecton_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat, count = *dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sig);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(count));
}

struct track_handler ubi_protecton_validation_a_handler = {
    .write_raw = ubi_protecton_validation_write_raw,
    .read_raw = ubi_protecton_validation_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x552aa549
    }
};

struct track_handler ubi_protecton_validation_b_handler = {
    .write_raw = ubi_protecton_validation_write_raw,
    .read_raw = ubi_protecton_validation_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x5524a529
    }
};

/*
 * UBI Protection
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u16 Raw Data - Length is variable even with different copies of the same game
 *  u32 Signature (0x5524a529 or 0x552aa549)
 *
 * After the sync is found the raw data is read into buffer u16 with a size of
 * 13000/2.  No game so far required a larger raw buffer.  The raw buffer is then
 * shifted by 1 to the right and a check for the signature is done. If the 
 * signature is not found then the shift and check is repeated up to 0x40 times.
 * Once the signature is found the offest is noted and the the data is decoded
 * from the offset-1 to the start of the raw buffer. A check for the value 
 * 0x00020001 is done at location offset-1 after it is decoded to make sure the
 * data is correct and the protection counter must be > 0xc00. 
 * 
 * The size of the decoded buffer is not known until the signature is found in
 * the shifting of the data.
 * 
 * Note: This version passes the raw data to the read method and writes it 
 * with the shift intact for a exact copy of the protection track.
 * 
 */

static void *ubi_protecton_raw_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ubi_protecton_info *info = handlers[ti->type]->extra_data;
    char *block;


    while (stream_next_bit(s) != -1) {
        uint32_t raw[2];
        uint16_t raw_buffer[13000/2], raw_buffer_decode[13000/2];
        unsigned int i, j, k, protection_counter, offset;
        uint16_t previous, current, upper;

        // sync
        if (s->word != 0x44894489) {
            continue;
        }
        ti->data_bitoff = s->index_offset_bc - 31;

        // read data and store into raw buffer
        raw_buffer[0] = 0;
        raw_buffer[1] = 0x4489;
        for (i = 2; i < sizeof(raw_buffer)/2; i++){
           if (stream_next_bits(s, 16) == -1)
                goto fail;
            raw_buffer[i] = (uint16_t)s->word;
        }

        offset = 0;

        memcpy(raw_buffer_decode, raw_buffer, i*2);

        // shift raw data until signature found - mimic assembly code
        for (k = 0; k < 0x40; k++) {
            for (j = i-1; j > 0; j--) {
                previous = raw_buffer_decode[j-1];
                current = raw_buffer_decode[j];
                upper = (uint16_t)(((previous << 16) | current) >> 1);
                raw_buffer_decode[j] = upper;
            }
            for (j = 0x2c00/2; j < i-1; j++) {
                if (((uint32_t)(raw_buffer_decode[j-1] << 16) | raw_buffer_decode[j]) == info->sig) {
                    offset = j-1;
                    k = 0x40;
                    break;
                }
            }

        }

        // Decode data and validate that the first decoded long before the sig is 0x00020001
        k = offset/4-1;
        uint32_t dat[offset/4+4];
        protection_counter = 0;
        for (j = offset-1; j > 3; j-=4) {
            raw[0] = be32toh((raw_buffer_decode[j-3] << 16) | raw_buffer_decode[j-2]);
            raw[1] = be32toh((raw_buffer_decode[j-1] << 16) | raw_buffer_decode[j]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[k]);
            protection_counter++;
            if((uint16_t)be32toh(dat[k]) == (uint16_t)protection_counter)
                protection_counter++;
            k--;
        }

        // verify that the first decoded value is 0x00020001 and that the decoded data 
        // size is greater than or equal to 0xc00. Lime & Napoleon fail as there is
        // partial data at the beginning of the track, but the data length is less than
        // 0xc00 and all games tested have a protection count > 0xc30.
        if (be32toh(dat[offset/4-1]) != 0x00020001 || protection_counter < 0xc00)
            continue;

        // pass the raw data length in
        raw_buffer[1] = offset;

        set_all_sectors_valid(ti);
        ti->total_bits = offset*16+32;
        block = memalloc(2*offset+2);
        memcpy(block, &raw_buffer[1], 2*offset+2);
        return block;
    }
fail:
    return NULL;
}


static void ubi_protecton_raw_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i;

    // sync
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    // data
    for (i = 1; i < dat[0]+1; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[i]);
}

struct track_handler ubi_protecton_raw_a_handler = {
    .write_raw = ubi_protecton_raw_write_raw,
    .read_raw = ubi_protecton_raw_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x552aa549
    }
};

struct track_handler ubi_protecton_raw_b_handler = {
    .write_raw = ubi_protecton_raw_write_raw,
    .read_raw = ubi_protecton_raw_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x5524a529
    }
};

/*
 * UBI Protection
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 Data - Length is variable even with different copies of the same game
 *  u32 Signature (0x5524a529 or 0x552aa549)
 *
 * After the sync is found the raw data is read into buffer u16 with a size of
 * 13000/2.  No game so far required a larger raw buffer.  The raw buffer is then
 * shifted by 1 to the right and a check for the signature is done. If the 
 * signature is not found then the shift and check is repeated up to 0x40 times.
 * Once the signature is found the offest is noted and the the data is decoded
 * from the offset-1 to the start of the raw buffer. A check for the value 
 * 0x00020001 is done at location offset-1 after it is decoded to make sure the
 * data is correct and the protection counter must be > 0xc00. 
 * 
 * The size of the decoded buffer is not known until the signature is found in
 * the shifting of the data.
 * 
 * The protection check counter can be greater the the validation count but 
 * must be within 4 (1 version of Puff's Saga can have a variance of 6)
 * 
 * The decoded data is a counter, example
 * 0x00080007, 0x00060005, 0x00040003, 0x00020001
 */

static void *ubi_protecton_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ubi_protecton_info *info = handlers[ti->type]->extra_data;
    char *block;


    while (stream_next_bit(s) != -1) {
        uint32_t raw[2];
        uint16_t raw_buffer[13000/2], raw_buffer_decode[13000/2];
        unsigned int i, j, k, protection_counter, offset;
        uint16_t previous, current, upper;

        // sync
        if (s->word != 0x44894489) {
            continue;
        }
        ti->data_bitoff = s->index_offset_bc - 31;

        // read data and store into raw buffer
        raw_buffer[0] = 0;
        raw_buffer[1] = 0x4489;
        for (i = 2; i < sizeof(raw_buffer)/2; i++){
           if (stream_next_bits(s, 16) == -1)
                goto fail;
            raw_buffer[i] = (uint16_t)s->word;
        }

        offset = 0;

        memcpy(raw_buffer_decode, raw_buffer, i*2);

        // shift raw data until signature found - mimic assembly code
        for (k = 0; k < 0x40; k++) {
            for (j = 0x2c00/2; j < i-1; j++) {
                if (((uint32_t)(raw_buffer_decode[j-1] << 16) | raw_buffer_decode[j]) == info->sig) {
                    offset = j-1;
                    k = 0x40;
                    break;
                }
            }
            for (j = i-1; j > 0 && k < 0x40; j--) {
                previous = raw_buffer_decode[j-1];
                current = raw_buffer_decode[j];
                upper = (uint16_t)(((previous << 16) | current) >> 1);
                raw_buffer_decode[j] = upper;
            }
        }

        // Decode data and validate that the first decoded long before the sig is 0x00020001
        k = offset/4;
        uint32_t dat[offset/4+1];
        protection_counter = 0;
        for (j = offset-1; j > 3; j-=4) {
            raw[0] = be32toh((raw_buffer_decode[j-3] << 16) | raw_buffer_decode[j-2]);
            raw[1] = be32toh((raw_buffer_decode[j-1] << 16) | raw_buffer_decode[j]);
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[k]);
            protection_counter++;
            if((uint16_t)be32toh(dat[k]) == (uint16_t)protection_counter)
                protection_counter++;
            k--;
        }

        // verify that the first decoded value is 0x00020001 and that the decoded data 
        // size is greater than or equal to 0xc00. Lime & Napoleon fail as there is
        // partial data at the beginning of the track, but the data length is less than
        // 0xc00 and all games tested have a protection count > 0xc30.
        if (be32toh(dat[offset/4]) != 0x00020001 || protection_counter < 0xc00)
            continue;

        // pass the raw data length and padding in fist position of the array
        dat[0] = (offset << 16) | (((k+j)%2 == 0) ? k+j : 0);

        // If the data reads the first 2 uint32 of the decoded array then set to 0
        // so the value will not be encoded as part of the data in the read
        if (k+j == 0)
            dat[1] = 0;

        set_all_sectors_valid(ti);
        ti->total_bits = offset*16+32;
        block = memalloc(offset+4);
        memcpy(block, &dat, offset+4);
        return block;
    }
fail:
    return NULL;
}


static void ubi_protecton_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ubi_protecton_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i, start_offset, data_len, padding_len;

    // sync
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    padding_len = (uint16_t)dat[0];
    data_len = dat[0] >> 16;
    start_offset = (dat[1] == 0) ? 2 : 1;

    // padding - padding is not used, but need to be included to position
    // the data correctly
    for(i = 0; i < padding_len; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0xaa);

    // data
    for (i = start_offset; i < data_len/4+1; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sig);
}

struct track_handler ubi_protecton_a_handler = {
    .write_raw = ubi_protecton_write_raw,
    .read_raw = ubi_protecton_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x552aa549
    }
};

struct track_handler ubi_protecton_b_handler = {
    .write_raw = ubi_protecton_write_raw,
    .read_raw = ubi_protecton_read_raw,
    .extra_data = & (struct ubi_protecton_info) {
        .sig = 0x5524a529
    }
};