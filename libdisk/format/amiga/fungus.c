/*
 * disk/fungus.c
 *
 * Custom format by Colin Dooley (Fungus T Bogeyman) of Gremlin.
 *
 * Written in 2020 by Keir Fraser
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync for tracks 2-55, 58-85, 136-151
 *  u16 0x8944:: Sync for tracks 86-135
 *  u16 dat[0xc00][2] :: Interleaved even/odd words
 *  u16 csum[2] :: Even/odd words, ADD.w sum over data
 *
 * TRKTYP_fungus data layout:
 *  u8 sector_data[6144]
 */

#include <libdisk/util.h>
#include <private/disk.h>

const static uint16_t syncs[] = { 0xa245, 0x8a91, 0xa291 };

static void *fungus_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    int nr_valid_blocks = 0, nr_complete_blocks = 0;

    block = memalloc(ti->len);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint8_t hdr[6*2];
        uint16_t dat[1000*2], csum;
        int i;

        for (i = 0; i < ARRAY_SIZE(syncs); i++)
            if ((uint16_t)s->word == syncs[i])
                break;
        if (i == ARRAY_SIZE(syncs))
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;
        if (is_valid_sector(ti, i))
            continue;

        if (stream_next_bytes(s, hdr, 6*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 6, hdr, hdr);
        if (hdr[1] == 0) {
            /* Empty */
            set_sector_valid(ti, i);
            nr_valid_blocks++;
            continue;
        }

        /* Format ID */
        if (hdr[1] != 0xff)
            continue;

        /* Track nr */
        if (hdr[2] != (tracknr^1))
            continue;

        /* Sector nr */
        if (hdr[3] != i)
            continue;

        /* Data */
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm, 2000, dat, dat);
        csum = 0;
        for (i = 0; i < 1000; i++)
            csum += be16toh(dat[i]);
        if (csum != ((hdr[4] << 8) | hdr[5]))
            continue;

        set_sector_valid(ti, hdr[3]);
        nr_valid_blocks++;
        nr_complete_blocks++;
        memcpy(&block[2000*hdr[3]], dat, 2000);

        /* 12 bytes 00 */
//        stream_next_bits(s, 12*16);
    }

    if (nr_complete_blocks == 0)
        goto fail;

    ti->total_bits = 100500;
    return block;

fail:
    memfree(block);
    return NULL;
}

static void fungus_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t csum, *dat = (uint16_t *)ti->dat;
    unsigned int i, sec;
    const static uint32_t wsyncs[] = { 0xa2452245, 0x8a914a91, 0xa2912291 };

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, wsyncs[sec]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xff);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, tracknr^1);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, sec);
        csum = 0;
        for (i = 0; i < 1000; i++)
            csum += be16toh(dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, csum);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm, 2000, dat);
        dat += 1000;
        for (i = 0; i < 12; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    }
}

struct track_handler fungus_handler = {
    .bytes_per_sector = 2000,
    .nr_sectors = 3,
    .write_raw = fungus_write_raw,
    .read_raw = fungus_read_raw
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
