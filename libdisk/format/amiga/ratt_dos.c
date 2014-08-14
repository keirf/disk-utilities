/*
 * disk/ratt_dos.c
 * 
 * Custom format used by Anthony "Ratt" Crowther.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 <sync>
 *  u32 header[2]   :: Odd/even longs, contents NOTed
 *  u32 data[N][2]  :: Odd/even longs
 *  u32 csum[2]     :: Odd/even longs
 * 
 * TRKTYP_ratt_dos_* data layout:
 *  u8 sector_data[bytes_per_sector]
 *  u32 header
 *  u16 sync
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct ratt_file {
    uint8_t name[13];
    uint8_t first_trk, nr_trks, sync_idx;
    uint32_t pad, length;
};

static void *ratt_dos_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int max_longs = ti->bytes_per_sector / 4;
    uint16_t sync = 0x4522;

    if (tracknr != 2) {
        struct track_info *t2 = &d->di->track[2];
        struct ratt_file *f = (struct ratt_file *)&t2->dat[0xbc];
        if ((t2->type != TRKTYP_ratt_dos_1800) &&
            (t2->type != TRKTYP_ratt_dos_1810))
            return NULL;
        while (f->name[0] != '\0') {
            uint8_t last_trk = f->first_trk + f->nr_trks - 1;
            if ((f->first_trk <= 80) && (last_trk >= 80))
                last_trk++;
            if ((f->first_trk <= tracknr) && (last_trk >= tracknr))
                goto found;
            f++;
        }
        return NULL;
    found:
        sync = be16toh(((uint16_t *)t2->dat)[6+f->sync_idx]);
    }

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[0x604], header, csum, key, step;
        unsigned int i, nr_longs;
        char *block;

        if ((uint16_t)s->word != sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, raw);
        header = csum = ~be32toh(raw[0]);
        if ((nr_longs = (uint16_t)csum) == 0)
            nr_longs = max_longs;
        if (nr_longs > max_longs)
            return NULL;

        key = 0xeff478edu;
        step = 0xbffb7e5eu;
        for (i = 0; i < nr_longs; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            dat[i] = htobe32(be32toh(dat[i]) - key);
            key += step;
            csum += be32toh(dat[i]);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, raw);
        csum += be32toh(raw[0]);
        if (csum != 0)
            continue;

        ti->len = ti->bytes_per_sector = nr_longs * 4 + 6;
        block = memalloc(ti->len);
        memcpy(block, dat, nr_longs * 4);
        *(uint32_t *)&block[nr_longs * 4] = htobe32(header);
        *(uint16_t *)&block[nr_longs * 4 + 4] = htobe16(sync);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void ratt_dos_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t header, csum, key, step, *dat = (uint32_t *)ti->dat;
    uint16_t sync;
    unsigned int i, nr_longs;

    nr_longs = (ti->len - 6) / 4;
    csum = header = be32toh(dat[nr_longs]);
    sync = be16toh(*(uint16_t *)&dat[nr_longs+1]);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, ~header);

    key = 0xeff478edu;
    step = 0xbffb7e5eu;
    for (i = 0; i < nr_longs; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]) + key);
        key += step;
        csum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, -csum);
}

struct track_handler ratt_dos_1800_handler = {
    .bytes_per_sector = 0x1800,
    .nr_sectors = 1,
    .write_raw = ratt_dos_write_raw,
    .read_raw = ratt_dos_read_raw
};

struct track_handler ratt_dos_1810_handler = {
    .bytes_per_sector = 0x1810,
    .nr_sectors = 1,
    .write_raw = ratt_dos_write_raw,
    .read_raw = ratt_dos_read_raw
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
