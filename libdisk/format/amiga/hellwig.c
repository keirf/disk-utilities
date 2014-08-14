/*
 * disk/hellwig.c
 *
 * Custom format hellwig as used by Digitek/Axxiom/Rainbow Arts.
 *
 * Powerstyx
 * Danger Freak
 * Apprentice
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489  :: Sync (possibly x2)
 *  u8  0
 *  u32 dat[5120/4]
 *  u32 dat[6200/4] :: apprentice
 *  u32 checksum
 *
 * TRKTYP_* data layout:
 *  u8 sector_data[bytes_per_sector]
 *  u8 checksum_type | (nr_syncs << 4)
 */

#include <libdisk/util.h>
#include <private/disk.h>

enum checksum_type { CSUM_zeroes, CSUM_ones, CSUM_data };

static void *hellwig_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->bytes_per_sector/4], csum, sum;
        enum checksum_type checksum_type;
        unsigned int i, two_sync;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        two_sync = (s->word == 0x44894489);
        if (two_sync && (stream_next_bits(s, 16) == -1))
            goto fail;
        if (s->word != 0x44892aaa)
            continue;

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);
        csum = be32toh(csum);

        if (csum == 0) {
            checksum_type = CSUM_zeroes;
        } else if (csum == 0xffffffff) {
            checksum_type = CSUM_ones;
        } else {
            checksum_type = CSUM_data;
            if (csum != sum)
                continue;
        }

        ti->len += 1;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len-1);
        block[ti->len-1] = checksum_type | ((two_sync?2:1) << 4);
        ti->total_bits = 102000;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void hellwig_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    enum checksum_type checksum_type = ti->dat[ti->len-1] & 0xf;
    unsigned int i, nr_sync = ti->dat[ti->len-1] >> 4;

    while (nr_sync--)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);

    for (i = csum = 0; i < ti->bytes_per_sector/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
              (checksum_type == CSUM_zeroes) ? 0x00000000
              : (checksum_type == CSUM_ones) ? 0xffffffff
              : csum);
}

struct track_handler hellwig_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
};

struct track_handler apprentice_handler = {
    .bytes_per_sector = 6200,
    .nr_sectors = 1,
    .write_raw = hellwig_write_raw,
    .read_raw = hellwig_read_raw
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
