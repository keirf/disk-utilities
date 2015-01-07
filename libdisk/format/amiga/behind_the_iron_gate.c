/*
 * disk/behind_the_iron_gate.c
 *
 * Custom format as used by Behind the Iron Gate
 *
 * Written in 2015 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0xaaaa8951 ::  Sync
 *  u32 dat[6144/4]
 *  u32 checksum
 *  Check sum is calculated EOR.L D1,D0 ROR.L #1,D0 over all data
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t gate_sum(uint32_t w, uint32_t s)
{
    s ^= be32toh(w);
    return (s>>1) | (s<<31);
}

static void *behind_the_iron_gate_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;

        if (s->word != 0xaaaa8951)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum = gate_sum(dat[i], sum);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);

        if (sum != be32toh(csum))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;

}

static void behind_the_iron_gate_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa8951);

    for (i = sum = 0; i < ti->len/4; i++){
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        sum = gate_sum(dat[i], sum);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, sum);
}

struct track_handler behind_the_iron_gate_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = behind_the_iron_gate_write_raw,
    .read_raw = behind_the_iron_gate_read_raw
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
