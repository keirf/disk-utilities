/*
 * disk/taekwondo_master.c
 *
 * Custom format as used on TaeKwonDo Master by Mirage.
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x22442244 Sync
 *  u32 dat[ti->len/4]
 *
 * The length of the decoded data is 6252/4 u32s. The last 
 * u32 is used to validate the data.  The data 
 * validation sums up the decoded data and the last u32 and
 * should equal 0xffffffff.  The decoder validation initializes 
 * the sum variable to 1 so the validation check should equal 0. 
 * The track loader/decoder never validates the data
 * 
 * 
 * TRKTYP_taekwondo_master data layout:
 *  u8 sector_data[6256]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *taekwondo_master_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0x22442244)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        sum = 1;
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if (sum != 0)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void taekwondo_master_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22442244);

    for (i = sum = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }
}

struct track_handler taekwondo_master_handler = {
    .bytes_per_sector = 6256,
    .nr_sectors = 1,
    .write_raw = taekwondo_master_write_raw,
    .read_raw = taekwondo_master_read_raw
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
