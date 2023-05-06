/*
 * disk/fuzzball.c
 *
 * Custom format as used on Fuzzball by System 3
 *
 * Written in 2023 by Keith Krellwitz
 *
 * Levels 14, 32, 38, and 50 have data stored on tracks
 * 156, 157, 158, and 159 using a custom format.  The
 * length and the sync are different for each of these
 * tracks.  Track 155 is unformatted.
 * 
 * RAW TRACK LAYOUT:
 * 
 *  Track 156
 *   u16 0x4489 Sync
 *   u32 checksum - eor over decoded data
 *   u8  dat[1876] :: encoded as even/odd block
 *
 *  Track 157
 *   u16 0x1289 Sync
 *   u32 checksum - eor over decoded data
 *   u8  dat[1888] :: encoded as even/odd block
 * 
 *  Track 158
 *   u16 0x2244 Sync
 *   u32 checksum - eor over decoded data
 *   u8  dat[1904] :: encoded as even/odd block
 * 
 *  Track 159
 *   u16 0x1289 Sync
 *   u32 checksum - eor over decoded data
 *   u8  dat[1920] :: encoded as even/odd block
 *
 * TRKTYP_fuzzball data layout:
 * 
 *  u8 sector_data[1876] :: Track 156
 *  u8 sector_data[1888] :: Track 157
 *  u8 sector_data[1904] :: Track 158
 *  u8 sector_data[1920] :: Track 159
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>


struct fuzzball_info {
    int tracknr;
    int length;
    uint16_t sync;
};

const static struct fuzzball_info fuzzball_infos[] = {
    { 156, 1876, 0x4489 },
    { 157, 1888, 0x1289 },
    { 158, 1904, 0x2244 },
    { 159, 1920, 0x1289 }
};

static const struct fuzzball_info *find_fuzzball_info(int tracknr)
{
    const struct fuzzball_info *fuzzball_info;
    for (fuzzball_info = fuzzball_infos; fuzzball_info->tracknr != tracknr; fuzzball_info++)
        continue;
    return fuzzball_info;
}


static void *fuzzball_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    if (tracknr < 156 || tracknr > 159)
        goto fail;
    const struct fuzzball_info *fuzzball_info = find_fuzzball_info(tracknr);
    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[2*fuzzball_info->length/4], csum, sum;
        unsigned int i;
        char *block;

        /* sync */
        if ((uint16_t)s->word != fuzzball_info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* data */
        if (stream_next_bytes(s, dat, 2*fuzzball_info->length) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, fuzzball_info->length, dat, dat);

        /* calculate data checksum */
        for (i = sum = 0; i < fuzzball_info->length/4; i++) {
            sum ^= be32toh(dat[i]);
        }

        if (be32toh(csum) != sum)
            goto fail;

        block = memalloc(fuzzball_info->length);
        memcpy(block, dat, fuzzball_info->length);
        set_all_sectors_valid(ti);
        ti->total_bits = 106000;
        return block;
    }

fail:
    return NULL;
}

static void fuzzball_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct fuzzball_info *fuzzball_info = find_fuzzball_info(tracknr);
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, fuzzball_info->sync);

    /* checksum */
    for (i = sum = 0; i < fuzzball_info->length/4; i++) {
        sum ^= be32toh(dat[i]);
    }
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, fuzzball_info->length, dat);
}

struct track_handler fuzzball_handler = {
    .write_raw = fuzzball_write_raw,
    .read_raw = fuzzball_read_raw
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
