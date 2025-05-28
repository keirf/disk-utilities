/*
 * disk/fantasy_games.c
 *
 * Custom format for Fantasy Games by Silverbyte.
 *
 *
 * Written in 2025 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x89448944
 *  u16 dat[6272/2-2] 
 *  u32 Checksum
 *
 * The checksum is calcaulated by taking the sum of each u32 
 * of the decoded data eor'd with 0x22945567
 * 
 * TRKTYP_fantasy_games data layout:
 *  u8 sector_data[6272]
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>


static void *fantasy_games_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    

    while (stream_next_bit(s) != -1) {
        uint16_t raw[2], dat[ti->bytes_per_sector/2];
        uint32_t sum, csum;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x89448944)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;


        /* data */
        for (i = 0; i < ti->bytes_per_sector/2; i++) {
            if (stream_next_bytes(s, raw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
        }

        for (i = sum = 0; i < ti->bytes_per_sector/2-2; i+=2) {
            csum = (be16toh(dat[i]) << 16) | be16toh(dat[i+1]);
            sum += 0x22945567 ^ csum;
        }
        csum = (be16toh(dat[i]) << 16) | be16toh(dat[i+1]);

        if (csum != sum)
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

static void fantasy_games_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    uint32_t sum, csum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x89448944);

    /* data */
    for (i = sum = 0; i < ti->bytes_per_sector/2-2; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
        sum += be16toh(dat[i]);
    }

    /* checksum */
    for (i = sum = 0; i < ti->bytes_per_sector/2-2; i+=2) {
        csum = (be16toh(dat[i]) << 16) | be16toh(dat[i+1]);
        sum += 0x22945567 ^ csum;
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)(sum >> 16));
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (uint16_t)(sum));
}

struct track_handler fantasy_games_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = fantasy_games_write_raw,
    .read_raw = fantasy_games_read_raw
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
