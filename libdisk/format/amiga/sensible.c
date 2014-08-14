/*
 * disk/sensible.c
 * 
 * Custom format as used by various Sensible Software releases:
 *   Cannon Fodder
 *   Mega Lo Mania
 *   Wizkid
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  'S','O','S','6'
 *  u32 csum
 *  u32 tracknr^1
 *  u32 data[12*512/4]
 * MFM encoding of sectors:
 *  All odd bits, followed by all even bits.
 * 
 * TRKTYP_sensible data layout:
 *  u8 sector_data[12*512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SOS_SIG 0x534f5336u

static void *sensible_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw_dat[2*(12+ti->len)/4], csum = 0;
        unsigned int i;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, raw_dat, sizeof(raw_dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 12+ti->len, raw_dat, raw_dat);

        if ((be32toh(raw_dat[0]) != SOS_SIG) ||
            ((uint8_t)be32toh(raw_dat[2]) != (tracknr^1)))
            continue;

        for (i = 0; i < ARRAY_SIZE(raw_dat)/2; i++)
            csum += be32toh(raw_dat[i]);
        csum -= be32toh(raw_dat[1]) * 2;
        if (csum != 0)
            continue;

        block = memalloc(ti->len);
        memcpy(block, &raw_dat[3], ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void sensible_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, csum;
    unsigned int i, enc;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    csum = SOS_SIG + (tracknr ^ 1);
    for (i = 0; i < ti->len/4; i++)
        csum += be32toh(dat[i]);

    for (i = 0; i < 2; i++) {
        enc = (i == 0) ? bc_mfm_odd : bc_mfm_even;
        tbuf_bits(tbuf, SPEED_AVG, enc, 32, SOS_SIG);
        tbuf_bits(tbuf, SPEED_AVG, enc, 32, csum);
        tbuf_bits(tbuf, SPEED_AVG, enc, 32, tracknr^1);
        tbuf_bytes(tbuf, SPEED_AVG, enc, ti->len, dat);
    }
}

struct track_handler sensible_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = sensible_write_raw,
    .read_raw = sensible_read_raw
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
