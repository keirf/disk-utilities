/*
 * disk/psygnosis_c.c
 * 
 * Custom format as used by various Psygnosis releases:
 *   The Killing Game Show
 *   Nitro
 *   Armour-Geddon (v2 format)
 *   Obitus (v2 format)
 * 
 * Written in 2012 by Keir Fraser
 * 
 * Various custom formats + variants on these disks.
 * Most tracks are long (~105500 bits).
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define V1_METABLK_WORDS 166
#define V2_METABLK_WORDS 154

static uint16_t checksum(uint16_t *dat, unsigned int nr, unsigned int ver)
{
    unsigned int i;
    uint32_t sum = -2;

    for (i = 0; i < nr; i++) {
        /* Simulate M68K ADDX instruction */
        if (sum > 0xffff)
            sum = (uint16_t)(sum+1);
        sum += be16toh(dat[i]);
    }
    if (ver == 2)
        sum &= 0xfffa;
    return (uint16_t)sum;
}

static void *psygnosis_c_track0_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t dat[V1_METABLK_WORDS+1], raw[2];
    char *ablk, *block;
    unsigned int i, metablk_words, ver;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    for (ver = 1; ver <= 2; ver++) {

        stream_reset(s);

        metablk_words = (ver == 1) ? V1_METABLK_WORDS : V2_METABLK_WORDS;

        while (stream_next_bit(s) != -1) {

            if ((uint16_t)s->word != 0x428a)
                continue;
            ti->data_bitoff = s->index_offset_bc - 15;

            if ((ver == 2) &&
                ((stream_next_bits(s, 16) == -1) ||
                 ((uint16_t)s->word != 0xaaaa)))
                continue;

            for (i = 0; i < (metablk_words + 1); i++) {
                if (stream_next_bytes(s, raw, 4) == -1)
                    break;
                mfm_decode_bytes(bc_mfm_even_odd, 2, raw, &dat[i]);
            }

            if (checksum(&dat[1], metablk_words, ver) != be16toh(dat[0]))
                continue;

            init_track_info(ti, TRKTYP_psygnosis_c_track0);
            ti->len += metablk_words*2;
            ti->total_bits = 105500;
            block = memalloc(ti->len);
            memcpy(block, ablk, 512*11);
            memcpy(&block[512*11], &dat[1], metablk_words*2);
            memfree(ablk);
            return block;
        }
    }

fail:
    memfree(ablk);
    return NULL;
}

static void psygnosis_c_track0_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)(ti->dat + 512*11);
    unsigned int i, ver, metablk_words;

    metablk_words = (ti->len - 512*11) / 2;
    ver = (metablk_words == V1_METABLK_WORDS) ? 1 : 2;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x428a);
    if (ver == 2)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16,
              checksum(dat, metablk_words, ver));
    for (i = 0; i < metablk_words; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);
}

struct track_handler psygnosis_c_track0_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = psygnosis_c_track0_write_raw,
    .read_raw = psygnosis_c_track0_read_raw
};

struct track_metadata {
    uint8_t version;
    bool_t valid;
    char id[4];
    uint32_t decoded_len;
    uint32_t mask;
};

static bool_t track_metadata(
    struct disk *d, unsigned int tracknr, struct track_metadata *mdat)
{
    struct track_info *ti = &d->di->track[0];
    struct h {
        uint32_t id;
        uint8_t exc_flags, trk_singleton, trk_range_start, trk_range_end;
    } *h;

    if (ti->type != TRKTYP_psygnosis_c_track0)
        return 0;

    h = (struct h *)(ti->dat + 512*11);

    memcpy(mdat->id, &h->id, 4);

    mdat->valid = 1;
    if ((h->exc_flags & 1) && (tracknr == 0))
        mdat->valid = 0;
    if ((h->exc_flags & 2) && (tracknr == h->trk_singleton))
        mdat->valid = 0;
    if ((h->exc_flags & 4) && ((tracknr >= h->trk_range_start) &&
                               (tracknr <= h->trk_range_end)))
        mdat->valid = 0;

    mdat->version = ((ti->len - 512*11) == V1_METABLK_WORDS*2) ? 1 : 2;
    if (mdat->version == 1) {
        struct h1 {
            uint16_t trk[160];
            uint32_t disklen;
        } *h1 = (struct h1 *)(h + 1);
        mdat->decoded_len = be16toh(h1->trk[tracknr]);
        mdat->mask = !(mdat->decoded_len & 0x1000);
        mdat->decoded_len &= 0xfff;
    } else /* mdat->version == 2 */ {
        struct h2 {
            uint8_t trk[80][3];
            uint32_t disklen;
            uint8_t mask_bitmap[20];
        } *h2 = (struct h2 *)(h + 1);
        mdat->decoded_len = h2->trk[tracknr/2][0];
        mdat->decoded_len <<= (tracknr&1) ? 8 : 4;
        mdat->decoded_len &= 0xf00;
        mdat->decoded_len |= h2->trk[tracknr/2][1+(tracknr&1)];
        mdat->mask = !(h2->mask_bitmap[tracknr/8] & (0x80u >> (tracknr&7)));
    }

    mdat->mask = mdat->mask ? 0xaaaaaaaau : 0x55555555u;

    return 1;
}

static void *psygnosis_c_custom_rll_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct track_metadata mdat;
    unsigned int i;
    uint16_t raw[2], csum;
    uint32_t *dat, lsum;

    if (!track_metadata(d, tracknr, &mdat) ||
        !mdat.valid || !mdat.decoded_len)
        return NULL;

    dat = memalloc(mdat.decoded_len * 4);

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0x4429)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if ((mdat.version == 2) && (stream_next_bits(s, 16) == -1))
            break;

        if (stream_next_bytes(s, raw, 4) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw, raw);
        csum = be16toh(raw[0]);

        memset(dat, 0, mdat.decoded_len * 4);
        for (i = 0;;) {
            if (stream_next_bit(s) == -1)
                break;
            dat[i/32] |= (s->word & 1u) << (31-(i&31));
            if (++i == mdat.decoded_len*32)
                break;
            if (s->word & 1) {
                /* D=1, C=0 */
                if ((stream_next_bit(s) == -1) || ((s->word & 1) != 0))
                    break;
            } else {
                if (stream_next_bit(s) == -1)
                    break;
                dat[i/32] |= (s->word & 1u) << (31-(i&31));
                if (++i == mdat.decoded_len*32)
                    break;
                if (s->word & 1) {
                    /* D=01, C=0 */
                    if ((stream_next_bit(s) == -1) || ((s->word & 1) != 0))
                        break;
                } else {
                    /* D=00, C=10 */
                    if ((stream_next_bits(s, 2) == -1) || ((s->word & 3) != 2))
                        break;
                }
            }
        }
        if (i != mdat.decoded_len*32)
            continue;
        for (i = 0, lsum = 0; i < mdat.decoded_len; i++) {
            dat[i] ^= mdat.mask;
            lsum += dat[i];
            dat[i] = htobe32(dat[i]);
        }
        lsum ^= lsum >> 16;
        lsum &= (mdat.version == 2) ? 0xfffa : 0xfff0;
        if (csum != lsum)
            continue;

        ti->len = mdat.decoded_len * 4;
        ti->total_bits = 105500;
        return dat;
    }

    memfree(dat);
    return NULL;
}

static void psygnosis_c_custom_rll_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, *dat = (uint32_t *)ti->dat;
    struct track_metadata mdat;
    unsigned int i, bits = 0;

    if (!track_metadata(d, tracknr, &mdat) ||
        !mdat.valid || !mdat.decoded_len)
        BUG();

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4429);
    if (mdat.version == 2)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xfc);

    for (i = 0, csum = 0; i < mdat.decoded_len; i++)
        csum += be32toh(dat[i]);
    csum ^= csum >> 16;
    csum &= (mdat.version == 2) ? 0xfffa : 0xfff0;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, csum);

    for (i = 0; i < mdat.decoded_len*32; i++) {
        if ((be32toh(dat[i/32])^mdat.mask) & (1u << (31-(i&31)))) {
            /* D=1 C=0 */
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 2, 0x2);
            bits += 2;
        } else if ((++i >= mdat.decoded_len*32) ||
                   !((be32toh(dat[i/32])^mdat.mask) & (1u << (31-(i&31))))) {
            /* D=00 C=10 */
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 4, 0x2);
            bits += 4;
        } else {
            /* D=01 C=0 */
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 3, 0x2);
            bits += 3;
        }
    }

    if (bits & 31)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32-(bits&31), 0xaaaaaaaau);
}

struct track_handler psygnosis_c_custom_rll_handler = {
    .write_raw = psygnosis_c_custom_rll_write_raw,
    .read_raw = psygnosis_c_custom_rll_read_raw
};

static void *psygnosis_c_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct track_metadata mdat;
    uint32_t dat[0x627], raw[2], *block;
    unsigned int i, nr_bytes = 0;

    if (tracknr == 0)
        return handlers[TRKTYP_psygnosis_c_track0]->write_raw(d, tracknr, s);

    if (!track_metadata(d, tracknr, &mdat))
        return NULL;

    if (mdat.valid && mdat.decoded_len) {
        init_track_info(ti, TRKTYP_psygnosis_c_custom_rll);
        return handlers[TRKTYP_psygnosis_c_custom_rll]
            ->write_raw(d, tracknr, s);
    }

    /* Nitro, Track 2: High-score table. */
    if (!strncmp(mdat.id, "tb_1", 4) && (tracknr == 2))
        nr_bytes = 0x189a;

    /* Killing Game Show, Disk 2, Track 159: High-score table. */
    if (!strncmp(mdat.id, "KGS2", 4) && (tracknr == 159))
        nr_bytes = 0x330;

    if (nr_bytes == 0)
        return NULL;

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0x4429)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < (nr_bytes+2+3)/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                break;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (checksum((uint16_t *)dat+1, nr_bytes/2, mdat.version) !=
            be16toh(*(uint16_t *)dat))
            continue;

        init_track_info(ti, TRKTYP_psygnosis_c);
        ti->len = nr_bytes;
        ti->total_bits = 105500;
        block = memalloc(ti->len);
        memcpy(block, (uint16_t *)dat+1, ti->len);
        return block;
    }

    return NULL;
}

static void psygnosis_c_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[0x627] = { 0 };
    struct track_metadata mdat;
    unsigned int i;

    if (!track_metadata(d, tracknr, &mdat) ||
        mdat.valid || mdat.decoded_len)
        BUG();

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4429);

    *(uint16_t *)dat = htobe16(
        checksum((uint16_t *)ti->dat, ti->len/2, mdat.version));
    memcpy((uint8_t *)dat + 2, ti->dat, ti->len);

    for (i = 0; i < (ti->len+2+3)/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
}

struct track_handler psygnosis_c_handler = {
    .write_raw = psygnosis_c_write_raw,
    .read_raw = psygnosis_c_read_raw
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
