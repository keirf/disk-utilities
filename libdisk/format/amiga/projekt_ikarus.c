/*
 * disk/projekt_ikarus.c
 * 
 * Custom format as used on Projekt Ikarus by Data Becker.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x89448944 :: Sync
 *  u16 dat[ti->len/2] - Last 2 words are the checksum
 * 
 * The checksum is the sum of each decoded u32 eor'd with 0x22568229
 * 
 * TRKTYP_projekt_ikarus data layout:
 *  u8 sector_data[6272]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *projekt_ikarus_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t dat[ti->len/2], raw[2];
        uint32_t sum;
        unsigned int i;
        char *block;

        if (s->word != 0x89448944)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        for (i = 0; i < ti->len/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
        }

        for (i = sum = 0; i < ti->len/2-2; i+=2) {
            sum += 0x22568229^((be16toh(dat[i]) << 16) | be16toh(dat[i+1]));
        }

        if(sum != ((be16toh(dat[ti->len/2-2]) << 16) | be16toh(dat[ti->len/2-1])))
            continue;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void projekt_ikarus_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint32_t csum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x89448944);

    for (i = csum = 0; i < ti->len/2-2; i+=2) {
        csum += 0x22568229^((be16toh(dat[i]) << 16) | be16toh(dat[i+1]));
    }

    for (i = 0; i < ti->len/2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
    }
}

struct track_handler projekt_ikarus_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = projekt_ikarus_write_raw,
    .read_raw = projekt_ikarus_read_raw
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
