/*
 * disk/discovery.c
 *
 * Custom format as used on Sword of Sodan, Arkanoid, Hybris, Zoom
 * by Discovery/Innerprise.
 *
 * Written in 2014/2016/2017 by Keith Krellwitz
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
 *
 * TRKTYP_arkanoid_a data layout:
 *  u8 sector_data[6472]
 *
 * TRKTYP_arkanoid_b data layout:
 *  u8 sector_data[6688]
 *
 * TRKTYP_arkanoid_c data layout:
 *  u8 sector_data[6720]
 *
 * TRKTYP_hybris data layout:
 *  u8 sector_data[6272]
 */

#include <libdisk/util.h>
#include <private/disk.h>


const static uint16_t syncs[] = {
    0x5412, 0x2145, 0x2541, 0x4252, 0x4489, 0x5241,
    0x9521, 0x448A, 0xA424, 0xA425, 0xA429, 0xA484,
    0x2144
};

static uint16_t discovery_sum(uint16_t w, uint16_t s)
{
    s ^= be16toh(w);
    return (s>>1) | (s<<15);
}

static void *discovery_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t sync, csum, sum, chk1, chk2, len1, len2;;
    uint16_t raw[2*ti->len/2], dat[(ti->len+6)/2], craw[2];
    unsigned int i, k;
    char *block;

    for (k = 0; k < ARRAY_SIZE(syncs); k++) {

        sync = syncs[k];
        while (stream_next_bit(s) != -1) {

            if ((uint16_t)s->word != sync)
                continue;

            ti->data_bitoff = s->index_offset_bc - 15;

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &sum);
            if (ti->type == TRKTYP_sword_sodan){
                if (sum != 0)
                    continue;
            }
            else if (ti->type == TRKTYP_arkanoid_a || ti->type == TRKTYP_arkanoid_b 
                || ti->type == TRKTYP_arkanoid_c){
                if (be16toh(sum) != sync)
                    continue;
            }

            if (stream_next_bytes(s, craw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &chk1);

            if (stream_next_bytes(s, craw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &len1);
            if (be16toh(len1) != (uint16_t)ti->len)
                continue;

            if (stream_next_bytes(s, craw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &len2);

            if (stream_next_bytes(s, raw, 2*ti->len) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

            if (stream_next_bytes(s, craw, 4) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &chk2);

            sum = discovery_sum(sum, 0);
            sum = discovery_sum(chk1, sum);
            sum = discovery_sum(len1, sum);
            sum = discovery_sum(len2, sum);
            for (i = 0 ; i < ti->len/2; i++)
                sum = discovery_sum(dat[i], sum);
            sum = discovery_sum(chk2, sum);

            if (stream_next_bytes(s, craw, 4) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 2, craw, &csum);
            if (sum != be16toh(csum))
                continue;

            /* No calculation for the data length and chk1 depends
             * on length in cases when the length is less than 0x1880.
             * dat is extended by 6 bytes. */
            dat[ti->len/2] = be16toh(chk1);
            dat[ti->len/2+1] = be16toh(len2);
            dat[ti->len/2+2] = sync;

            stream_next_index(s);
            ti->total_bits = (s->track_len_bc > 102500) ? (s->track_len_bc > 104400)
                ? 108000 : 104300 : 102300;

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

static void discovery_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t sum, val, *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, dat[ti->len/2+2]);

    val = (ti->type == TRKTYP_sword_sodan) ? 0 :
        (ti->type == TRKTYP_hybris) ? 0 : dat[ti->len/2+2];

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, val);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, dat[ti->len/2]);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, ti->len);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, dat[ti->len/2+1]);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, 0xdead);

    sum = discovery_sum(be16toh(val), 0);
    sum = discovery_sum(be16toh(dat[ti->len/2]), sum);
    sum = discovery_sum(be16toh(ti->len), sum);
    sum = discovery_sum(be16toh(dat[ti->len/2+1]), sum);

    for (i = 0 ; i < ti->len/2; i++)
        sum = discovery_sum(dat[i], sum);
    sum = discovery_sum(be16toh(0xdead), sum);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, sum);
}

struct track_handler arkanoid_a_handler = {
    .bytes_per_sector = 6472,
    .nr_sectors = 1,
    .write_raw = discovery_write_raw,
    .read_raw = discovery_read_raw
};

struct track_handler arkanoid_b_handler = {
    .bytes_per_sector = 6688,
    .nr_sectors = 1,
    .write_raw = discovery_write_raw,
    .read_raw = discovery_read_raw
};

struct track_handler arkanoid_c_handler = {
    .bytes_per_sector = 6720,
    .nr_sectors = 1,
    .write_raw = discovery_write_raw,
    .read_raw = discovery_read_raw
};

struct track_handler hybris_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = discovery_write_raw,
    .read_raw = discovery_read_raw
};

struct track_handler sword_sodan_handler = {
    .bytes_per_sector = 6272,
    .nr_sectors = 1,
    .write_raw = discovery_write_raw,
    .read_raw = discovery_read_raw
};


static void *zoom_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], csum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        /* Track 118 on the NTSC version only has
         * 2 sync words and the PAL version has
         * three.*/
        if (s->word == 0xaaaa4489){
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if ((uint16_t)s->word != 0x4489)
                continue;
            ti->data_bitoff = s->index_offset_bc - 31;
            if (ti->type == TRKTYP_zoom_b){
                if (stream_next_bits(s, 16) == -1)
                    goto fail;
                if ((uint16_t)s->word != 0x4489)
                    continue;
                ti->data_bitoff = s->index_offset_bc - 47;
            }
        } else {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x44894489)
                continue;
            ti->data_bitoff = s->index_offset_bc - 47;
        }

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if((uint8_t)(0xff&be32toh(dat[0])) != (uint8_t)~(tracknr^~1))
            continue;

        csum = 0;
        for (i = csum = 0; i < ti->len/4 - 5; i++)
                csum ^= be32toh(dat[i+2]);
        if (csum != be32toh(dat[1]))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 102300;
        return block;
    }

fail:
    return NULL;
}



static void zoom_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    for (i = 0; i < ti->len/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler zoom_a_handler = {
    .bytes_per_sector = 6164,
    .nr_sectors = 1,
    .write_raw = zoom_write_raw,
    .read_raw = zoom_read_raw
};
struct track_handler zoom_b_handler = {
    .bytes_per_sector = 6164,
    .nr_sectors = 1,
    .write_raw = zoom_write_raw,
    .read_raw = zoom_read_raw
};


static int check_sequence(struct stream *s, unsigned int nr, uint8_t byte)
{
    while (--nr) {
        stream_next_bits(s, 16);
        if ((uint8_t)mfm_decode_word(s->word) != byte)
            break;
    }
    return !nr;
}

static int check_length(struct stream *s, unsigned int min_bits)
{
    stream_next_index(s);
    return (s->track_len_bc >= min_bits);
}

static void *zoom_prot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {
        ti->data_bitoff = s->index_offset_bc - 15;
        if (!check_sequence(s, 1000, 0xaa))
            continue;
        if (!check_length(s, 102000))
            break;

         ti->total_bits = 102386;
        return memalloc(0);
    }

    return NULL;
}



static void zoom_prot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489 );
    for (i = 0; i < 6396; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xaa);

}
// Protetion not finished
struct track_handler zoom_prot_handler = {
    .write_raw = zoom_prot_write_raw,
    .read_raw = zoom_prot_read_raw
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

