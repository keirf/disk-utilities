/*
 * disk/skaut.c
 *
 * Custom format as used on Cyber World by Magic Bytes and 
 * Subtrade: Return To Irata from boeder.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4425 Sync
 *  u32 dat[ti->len/4]
 * 
 * The checksum is not stored on disk, but in code. The sum of
 * the raw data needs to equal 0xab5de67a
 *
 * TRKTYP_skaut data layout:
 *  u8 sector_data[5120]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t add_with_carry(uint32_t sum, uint32_t value) {
    uint64_t temp = (uint64_t)sum + (uint64_t)value;

    return (uint32_t)temp + (uint32_t)(temp >> 32);
}

static void *skaut_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0xaaaa4425)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum = add_with_carry(sum, be32toh(raw[0]));
            if (i < ti->len/4-1)
                sum = add_with_carry(sum, be32toh(raw[1]));
        }
        
        sum += 4;

        if (sum != 0xab5de67a)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101025;
        return block;
    }

fail:
    return NULL;
}

static void skaut_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa4425);

    for (i = 0; i < ti->len/4; i++) {
        if (be32toh(dat[i]) == 0xece44e2d) {
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x5452A514);
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44444425);
        } 
        else if (i == 1510 && be32toh(dat[i]) == 0x1cccb31f) {
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x24445125);
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x14449115);
        }
        else
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }

}

struct track_handler skaut_protection_handler = {
    .bytes_per_sector = 6248,
    .nr_sectors = 1,
    .write_raw = skaut_write_raw,
    .read_raw = skaut_read_raw
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
