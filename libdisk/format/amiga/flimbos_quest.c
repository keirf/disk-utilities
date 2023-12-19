/*
 * disk/flimbos_quest.c
 *
 * Custom format as used on Flimbo's Quest by System 3.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u32 0x464c494d :: sig 'FLIM'
 *  u32 checksum :: sum of decoded data
 *  u32 dat[5632/4]
 *
 * TRKTYP_flimbos_quest_a data layout:
 *  u8 sector_data[5632]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SIG_FLIM 0x464c494d

static void *flimbos_quest_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sig, csum, sum;
        unsigned int i;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);
        if (be32toh(sig) != SIG_FLIM)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void flimbos_quest_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    // sync
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    // sig
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, SIG_FLIM);

    // checksum
    for (i = sum = 0; i < ti->len/4; i++)
        sum += be32toh(dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    // data
    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    
}

struct track_handler flimbos_quest_a_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = flimbos_quest_a_write_raw,
    .read_raw = flimbos_quest_a_read_raw
};


/*
 * TRKTYP_flimbos_quest_b 
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u32 0x2aaaaaaa :: padding
 *  u32 0xA9292912 :: 2nd sync
 *  u32 0x4A554AA9 :: 3rd sync 
 *  u32 EOR value used for the checksum :: track 2-14 always 
 *      use 0xaaaaaaaa, but in one version the value was incorrect
 *      on the disk
 *  u16 0x2aaa :: padding
 *  u32 0xaaaa44A2 :: 4th sync
 *  u16 0x2aaa :: padding
 *  u32 0x464c494d :: sig 'FLIM'
 *  u32 checksum :: sum of decoded data ^ (EOR value)
 *  u32 dat[5632/4]
 * 
 * Out of 4 different raw dumps, 2 returned a couple of bad eor 
 * values. I added an array to compare the the values and if they
 * are different it uses the one from the array rather than the
 * one from disk. After adding the array the checksums of the
 * two dumps with bad eor's decoded correctly
 * 
 * TRKTYP_flimbos_quest_b data layout:
 *  u8 sector_data[5632]
 */

// 
static const uint32_t eor_array[];

static unsigned int get_track_length(unsigned int trk_nbr) {

        switch (trk_nbr) {
        case 14:
            return 972;
        case 28:
            return 768;
        case 44:
            return 444;
        case 57:
            return 4344;
        case 69:
            return 3100;
        case 82:
            return 3636;
        case 95:
            return 556;
        case 102:
            return 2692;
        default:
            return 5632;
        }
}


static void *flimbos_quest_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t eor_value = 0xaaaaaaaa;

    if (tracknr < 2 || tracknr >102)
        goto fail;

    while (stream_next_bit(s) != -1) {
        // sync
        if (s->word == 0x44894489)
            break;
    }
    ti->data_bitoff = s->index_offset_bc - 31;

    while (stream_next_bit(s) != -1) {

        // sync
        if (s->word != 0xA9292912)
            continue;

        // constant
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x4A554AA9)
            continue;

        // eor value for the tracks 2-14 is always 0xaaaaaaaa
        // one version the value on disk is incorrect for track 10
        // fixed when ipf is created
        if (stream_next_bits(s, 32) == -1)
            goto fail;

        if (tracknr > 14)
            eor_value = s->word;

        if (eor_value != eor_array[tracknr-2]) {
            //trk_warn(ti, tracknr, "The eor value on disk does not match. Attempting to repair");
            eor_value = eor_array[tracknr-2];
        }

        break;
    }

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], dat[ti->len/4+1], sig, csum, sum;
        unsigned int i,trk_len;
        char *block;
        
        // sync
        if (s->word != 0xaaaa44A2)
            continue;

        // Padding - never checked
        if (stream_next_bits(s, 16) == -1)
            goto fail;

        // Check SIG
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &sig);
        if (be32toh(sig) != SIG_FLIM)
            continue;

        // Checksum
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        // Get track length
        trk_len =  get_track_length(tracknr);

        // Decode Data
        for (i = sum = 0; i < trk_len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]) ^ eor_value;
        }

        // Checksum check
        if (be32toh(csum) != sum)
            continue;

        // store eor value
        dat[ti->len/4] = eor_value;

        block = memalloc(ti->len+4);
        memcpy(block, dat, ti->len+4);
        set_all_sectors_valid(ti);
        ti->total_bits = 100500;
        return block;
    }

fail:
    return NULL;
}

static void flimbos_quest_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i, trk_len;

    // sync
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    // padding
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x2aaaaaaa);
    // constant
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xA9292912);
    // constant
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4A554AA9);
    // eor value
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[ti->len/4]);
    // padding
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2aaa);
    // constant
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa44a2);
    // padding
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xaaaa);
    // SIG
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, SIG_FLIM);

    // Checksum
    trk_len =  get_track_length(tracknr);
    for (i = sum = 0; i < trk_len/4; i++)
        sum += be32toh(dat[i]) ^ dat[ti->len/4];
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    // data
    for (i = 0; i < trk_len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler flimbos_quest_b_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = flimbos_quest_b_write_raw,
    .read_raw = flimbos_quest_b_read_raw
};

static const uint32_t eor_array[] = {
    0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa,
    0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa,
    0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa,
    0xaaaaaaaa, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa9495445, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa9495445, 0x45445251, 0x51514491, 0xa954aaa9,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa9495445, 0x45445251, 0x51514491,
    0xa954aaa9, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa9495445, 0x45445251,
    0x51514491, 0xa954aaa9, 0xa954aaa9, 0xa9495445,
    0x45445251, 0x51514491, 0xa954aaa9, 0xa9495445,
    0x45445251
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
