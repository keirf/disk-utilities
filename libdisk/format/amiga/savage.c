/*
 * disk/savage.c
 *
 * Custom format as used by Savage from MicroPlay/Firebird:
 *
 * Written in 2014 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x4489 :: Sync
 *  u32 header[2] :: Even/odd
 *  u32 header2[2] :: Even/odd
 *  u32 csum[2] :: Even/odd
 *  u8  data[11][512][2] :: Even/odd blocks
 *  u32 zero[2] :: Even/odd
 *  Header is 0xff000000u | track number << 16 | current sector << 8 |
 *  (total sectors - current sector)
 *
 * TRKTYP_savage data layout:
 *  u8 sector_data[11][512]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *savage_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t csum, sum, trk, enctrk, zero, raw[2],
            raw2[2*ti->bytes_per_sector/4],
            dat[ti->nr_sectors*ti->bytes_per_sector/4];
        unsigned int i;
        unsigned int sec;
        char *block;

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (sec = 0; sec < ti->nr_sectors; sec++) {

            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &trk);
            if (tracknr!= ((be32toh(trk) >> 16) & 0xff))
                break;

            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &enctrk);

            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

            if (stream_next_bytes(s, raw2, 2*ti->bytes_per_sector) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector,
                raw2, &dat[sec*ti->bytes_per_sector/4]);

            for (i = sum = 0; i < 0x100; i++)
                sum ^= be32toh(raw2[i]) ;
            if (be32toh(csum) != (sum & 0x55555555))
                break;

            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &zero);
            if (be32toh(zero) != 0)
                break;

            if (sec < (ti->nr_sectors-1)){
                if (stream_next_bits(s, 32) == -1)
                    goto fail;
                if (s->word != 0x44894489)
                    break;
            }
        }

        if (sec != ti->nr_sectors)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static uint32_t csum_long(uint32_t w_prev, uint32_t w)
{
    uint32_t e = 0, o = 0, csum = 0;
    unsigned int i;

    for (i = 0; i < 16; i++) {
        e = (e << 1) | ((w >> 31) & 1);
        o = (o << 1) | ((w >> 30) & 1);
        w <<= 2;
    }

    csum ^= mfm_encode_word((w_prev << 16) | e);
    csum ^= mfm_encode_word((e << 16) | o);
    return csum;
}


static void savage_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t csum, prev, hdr, hdr2, *dat = (uint32_t *)ti->dat;
    unsigned int sec, i, base;

    for (sec = base = 0; sec < ti->nr_sectors; sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

        hdr = 0xff000000u | tracknr<<16 | sec<<8 | (ti->nr_sectors-sec);
        hdr2 = csum_long(0x44894489, hdr) & 0x55555555;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr2);

        prev = 0x44894489; /* get 1st clock bit right for checksum */
        for (i = csum = 0; i < ti->bytes_per_sector/4; i++) {
            csum ^= csum_long(prev, be32toh(dat[sec*ti->bytes_per_sector/4+i]));
            prev = be32toh(dat[sec*ti->bytes_per_sector/4+i]);
        }
        csum &= 0x55555555;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector,
            &ti->dat[sec*ti->bytes_per_sector]);

        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    }

}

struct track_handler savage_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = savage_write_raw,
    .read_raw = savage_read_raw
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
