/*
 * disk/zyconix.c
 *
 * Custom format as used on Zyconix by Accolade
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 hdr[20/4]
 *  u32 dat[ti->len/4]
 *  u32 checksum
 *
 * The checksum is the sum of the decoded data and header
 * 
 * TRKTYP_zyconix data layout:
 *  u8 sector_data[5632]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* graemes format */
#define SIG_1 0x00006772    /* ##gr */
#define SIG_2 0x61656d65    /* aeme */
#define SIG_3 0x7320666f    /* s fo */
#define SIG_4 0x726d6174    /* rmat */


static void *zyconix_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t hdr[2*20/4], dat[2*ti->len/4], raw[2], csum, sum;
        unsigned int i;
        char *block;

        /* sync */
        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        /* header */
        if (stream_next_bytes(s, hdr, 2*20) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 20, hdr, hdr);

        /* add decoded header to the checksum */
        for (i = sum = 0; i < 5; i++) {
            sum += be32toh(hdr[i]);
        }

        /* validate header */
        if ((SIG_1 | tracknr << 24 | tracknr << 16) != be32toh(hdr[0]))
            continue;

        if (SIG_2 != be32toh(hdr[1]))
            continue;

        if (SIG_3 != be32toh(hdr[2]))
            continue;

        if (SIG_4 != be32toh(hdr[3]))
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        /* add decoded data to the checksum */
        for (i = 0; i < ti->len/4; i++) {
            sum += be32toh(dat[i]);
        }

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        if (be32toh(csum) != sum)
            goto fail;

        stream_next_index(s);
        block = memalloc(ti->len+20);
        memcpy(block, hdr, 20);
        memcpy(&block[20], dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void zyconix_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* padding */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    /* set sig 1 */
    dat[0] = be32toh((SIG_1 | tracknr << 24 | tracknr << 16));

    /* header*/
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 20, &dat[0]);
    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, &dat[5]);

    /* calcaulate checksum */
    for (i = sum = 0; i < ti->len/4+5; i++)
        sum += be32toh(dat[i]);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

}

struct track_handler zyconix_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = zyconix_write_raw,
    .read_raw = zyconix_read_raw
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
