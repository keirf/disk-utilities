/*
 * disk/sword_sodan.c
 *
 * Custom format as used on Sword of Sodan by Discovery/Innerprise.
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 12 possible syncs :: Sync
 *  u16 0 :: Always 0
 *  u16 Next track or FFFF if data length < 0x1800 or track = 1
    (tracknr 79 on disk 1 & 2 = 81)
 *  u16 0x1880 :: Track length
 *  u16 Length of data on track
 *  u16 dat[6272/2]
 *  u16 0xdead
 *  u16 csum[2] :: EOR.W D1,D0 ROR.W #1,D0 over all data
 *
 * TRKTYP_sword_sodan data layout:
 *  u8 sector_data[6272]
 */

#include <libdisk/util.h>
#include "../private.h"

const static uint16_t syncs[] = {
    0x5412, 0x2145, 0x2541, 0x4252, 0x4489, 0x5241,
    0x9521, 0x448A, 0xA424, 0xA425, 0xA429, 0xA484
};

static uint16_t sodan_sum(uint16_t w, uint16_t s){
    s ^= be16toh(w);
    return (s>>1) | (s<<15) ;
}

static void *sword_sodan_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t sync, csum, sum, chk1, chk2, len1, len2;;
    uint16_t raw[2*ti->len/2], dat[(ti->len+6)/2], craw[2];
    unsigned int i, k;
    char *block;

    if (tracknr == 80 || tracknr >159)
        goto fail;

    for(k = 0; k < ARRAY_SIZE(syncs); k++){

        sync = syncs[k];

        while (stream_next_bit(s) != -1) {

            if (s->word != (0xaaaa0000 | sync))
                continue;

            ti->data_bitoff = s->index_offset - 15;

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &sum);
            if (sum != 0)
               continue;

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &chk1);

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &len1);
            if (be16toh(len1) != 0x1880)
                break;

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &len2);

            if (stream_next_bytes(s, raw, 2*ti->len) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &chk2);

            sum = sodan_sum(chk1,sum);
            sum = sodan_sum(len1,sum);
            sum = sodan_sum(len2,sum);
            for (i = 0 ; i < ti->len/2; i++)
                sum = sodan_sum(dat[i], sum);
            sum = sodan_sum(chk2,sum);

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &csum);

            if (sum != be16toh(csum))
                continue;

            // No calculation for the data length and chk1 depends
            // on length in cases when the length is less than 0x1880.
            // dat is extended by 6 bytes.
            dat[0xc40] = be16toh(chk1);
            dat[0xc41] = be16toh(len2);
            dat[0xc42] = sync;

            stream_next_index(s);
            ti->total_bits = (s->track_bitlen > 102500) ? 104300
                : 102300;

            block = memalloc(ti->len+6);
            memcpy(block, dat, ti->len+6);
            set_all_sectors_valid(ti);
            return block;
        }
        stream_reset(s);
    }

fail:
    return NULL;
}

static void sword_sodan_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t sum, chk, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[0xc42]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, dat[0xc40]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, 0x1880);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, dat[0xc41]);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, 0xdead);

    sum = 0;
    sum = sodan_sum(be16toh(dat[0xc40]), sum);
    sum = sodan_sum(be16toh(0x1880), sum);
    sum = sodan_sum(be16toh(dat[0xc41]), sum);
    for (i = 0 ; i < ti->len/2; i++)
        sum = sodan_sum(dat[i], sum);
    sum = sodan_sum(be16toh(0xdead), sum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, sum);
}

struct track_handler sword_sodan_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = sword_sodan_write_raw,
    .read_raw = sword_sodan_read_raw
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
