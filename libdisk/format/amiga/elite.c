/*
 * disk/elite.c
 *
 * Custom format as used by Elite/Capcom for the following games:
 *
 * Commando
 * Aquablast
 * Paperboy
 * Speed Buggy
 * Buggy Boy
 * Gremlins II
 *
 * Written in 2012 by Keir Fraser
 * Updated in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0xa245,0x4489
 *  u16 trk_even,trk_odd
 *  u32 data_even[0x600]
 *  u32 csum_even
 *  u32 data_odd[0x600]
 *  u32 csum_odd
 *  Checksum is 1 - sum of all decoded longs.
 *  Track length is normal (not long)
 *
 *  Track length is long for Buggy Boy & Gremlins II
 *
 * TRKTYP_elite_a data layout:
 *  u8 sector_data[6144]
 *
 * TRKTYP_elite_b data layout:
 *  u8 sector_data[5888]
 *
 * TRKTYP_elite_c data layout:
 *  u8 sector_data[6312]
 *
 * TRKTYP_elite_c data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *elite_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, dat[(ti->len/4+1)*2];
        uint16_t trk;
        unsigned int i;
        char *block;

        if (s->word != 0xa2454489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (ti->type != TRKTYP_elite_d) {
            if (stream_next_bytes(s, dat, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, dat, &trk);
            trk = be16toh(trk);
            if (trk != tracknr)
                continue;
        }
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, sizeof(dat)/2, dat, dat);

        csum = ~0u;
        for (i = 0; i < ti->len/4; i++)
            csum -= be32toh(dat[i]);
        if (csum != be32toh(dat[ti->len/4]))
            continue;

        if (ti->type != TRKTYP_elite_d)
            ti->total_bits = 105700;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

    return NULL;
}

static void elite_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, dat[(ti->len/4+1)*2];
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa2454489);
    if (ti->type != TRKTYP_elite_d)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, tracknr);

    memcpy(dat, ti->dat, ti->len);
    csum = ~0u;
    for (i = 0; i < ti->len/4; i++)
        csum -= be32toh(dat[i]);
    dat[ti->len/4] = htobe32(csum);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len+4, dat);
}

struct track_handler elite_a_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = elite_write_raw,
    .read_raw = elite_read_raw
};

struct track_handler elite_b_handler = {
    .bytes_per_sector = 5888,
    .nr_sectors = 1,
    .write_raw = elite_write_raw,
    .read_raw = elite_read_raw
};

struct track_handler elite_c_handler = {
    .bytes_per_sector = 6312,
    .nr_sectors = 1,
    .write_raw = elite_write_raw,
    .read_raw = elite_read_raw
};

struct track_handler elite_d_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = elite_write_raw,
    .read_raw = elite_read_raw
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
