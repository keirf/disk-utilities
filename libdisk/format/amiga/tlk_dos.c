/*
 * disk/tlk_dos.c
 * 
 * TLK-DOS Custom format used on TLK releases such as Tecnoball.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 <sync>,<sync>
 *  u16 ff54
 *  u16 ~tracknr
 *  u16 csum_lo, csum_hi
 *  u16 dat[6292/2]
 * Checksum is ADD.L over all words in dat[].
 * Encoding is odd/even MFM blocks. Block size and sync varies by track:
 * found via lookup in a table indexed by tracknr/4.
 * 
 * TRKTYP_tlk_dos data layout:
 *  u8 sector_data[6292]
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* Sync and encoding-block-size for each group of 4 tracks. */
struct track_param {
    uint16_t sync, blksz;
};

static void *tlk_dos_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s,
    uint16_t tlk_id, const struct track_param *track_param)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct track_param *param = &track_param[tracknr/4];
    uint32_t sync = ((uint32_t)param->sync << 16) | param->sync;

    if (tracknr >= 160)
        return NULL;

    while (stream_next_bit(s) != -1) {

        uint32_t csum, sum;
        uint16_t dat[6300];
        unsigned int i;
        char *block;

        if (s->word != sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            goto fail;
        for (i = 0; i < (6300/param->blksz); i++)
            mfm_decode_bytes(bc_mfm_odd_even, param->blksz,
                             &dat[i*param->blksz], &dat[(i*param->blksz)/2]);

        /* TLK-ID */
        if (be16toh(dat[0]) != tlk_id)
            continue;

        /* Track no. */
        if ((uint16_t)~be16toh(dat[1]) != tracknr)
            continue;

        /* Checksum */
        csum = 0;
        for (i = 4; i < 6300/2; i++)
            csum += be16toh(dat[i]);
        sum = be16toh(dat[2]) | ((uint32_t)be16toh(dat[3]) << 16);
        if (sum != csum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, &dat[4], ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = 101500;
        return block;
    }

fail:
    return NULL;
}

static void tlk_dos_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf,
    uint16_t tlk_id, const struct track_param *track_param)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct track_param *param = &track_param[tracknr/4];
    uint16_t dat[6300/2];
    uint32_t csum = 0;
    unsigned int i;

    memcpy(&dat[4], ti->dat, ti->len);
    for (i = 4; i < 6300/2; i++)
        csum += be16toh(dat[i]);

    dat[0] = htobe16(tlk_id);
    dat[1] = htobe16(~tracknr);
    dat[2] = htobe16(csum);
    dat[3] = htobe16(csum >> 16);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, param->sync);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, param->sync);

    for (i = 0; i < (6300/param->blksz); i++)
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, param->blksz,
                   &dat[(i*param->blksz)/2]);
}

const static struct track_param tlk1_param[] = {
    { 0x4489, 0x189c }, { 0x4489, 0x0032 },
    { 0x2a4d, 0x0064 }, { 0x2a8b, 0x0032 },
    { 0x4489, 0x0024 }, { 0x4489, 0x04ec },
    { 0x4489, 0x000a }, { 0x2a8b, 0x000a },
    { 0x4489, 0x001c }, { 0x2a8b, 0x0046 },
    { 0x2aad, 0x01c2 }, { 0x4489, 0x00d2 },
    { 0x2a8b, 0x00fc }, { 0x2aad, 0x0096 },
    { 0x4489, 0x00b4 }, { 0x4489, 0x002a },
    { 0x4489, 0x0046 }, { 0x2a8b, 0x007e },
    { 0x2aad, 0x0276 }, { 0x2aad, 0x012c },
    { 0x4489, 0x0014 }, { 0x4489, 0x0006 },
    { 0x2aad, 0x001e }, { 0x4489, 0x007e },
    { 0x2a4d, 0x0834 }, { 0x2a8b, 0x00b4 },
    { 0x2a8b, 0x0012 }, { 0x2aad, 0x04ec },
    { 0x2a8b, 0x0834 }, { 0x4489, 0x189c },
    { 0x4489, 0x189c }, { 0x4489, 0x189c },
    { 0x4489, 0x189c }, { 0x4489, 0x189c },
    { 0x4489, 0x189c }, { 0x4489, 0x189c },
    { 0x4489, 0x189c }, { 0x4489, 0x189c },
    { 0x4489, 0x189c }, { 0x4489, 0x189c }
};

static void *tlk_dos_1_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    return tlk_dos_write_raw(d, tracknr, s, 0xff54, tlk1_param);
}

static void tlk_dos_1_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    tlk_dos_read_raw(d, tracknr, tbuf, 0xff54, tlk1_param);
}

struct track_handler tlk_dos_1_handler = {
    .bytes_per_sector = 6292,
    .nr_sectors = 1,
    .write_raw = tlk_dos_1_write_raw,
    .read_raw = tlk_dos_1_read_raw
};

const static struct track_param tlk2_param[] = {
    { 0x4489, 0x189c }, { 0x4489, 0x189c }, 
    { 0x4489, 0x0064 }, { 0x2a8b, 0x0032 }, 
    { 0x4489, 0x0024 }, { 0x4489, 0x04ec }, 
    { 0x4489, 0x000a }, { 0x2a8b, 0x000a }, 
    { 0x4489, 0x001c }, { 0x2a8b, 0x0046 }, 
    { 0x2a8b, 0x0834 }, { 0x2aad, 0x01c2 }, 
    { 0x4489, 0x00d2 }, { 0x2a8b, 0x00fc }, 
    { 0x2aad, 0x0096 }, { 0x4489, 0x00b4 }, 
    { 0x4489, 0x002a }, { 0x4489, 0x0046 }, 
    { 0x2a8b, 0x007e }, { 0x2aad, 0x0276 }, 
    { 0x2aad, 0x012c }, { 0x4489, 0x0014 }, 
    { 0x4489, 0x0006 }, { 0x2aad, 0x001e }, 
    { 0x4489, 0x007e }, { 0x2a4d, 0x0834 }, 
    { 0x2a8b, 0x00b4 }, { 0x2a8b, 0x0012 }, 
    { 0x2aad, 0x04ec }, { 0x2aad, 0x189c }, 
    { 0x2aad, 0x189c }, { 0x2aad, 0x189c }, 
    { 0x2aad, 0x189c }, { 0x2aad, 0x189c }, 
    { 0x2aad, 0x189c }, { 0x2aad, 0x189c }, 
    { 0x2aad, 0x189c }, { 0x2aad, 0x189c }, 
    { 0x2aad, 0x189c }, { 0x2aad, 0x189c }
};

static void *tlk_dos_2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    return tlk_dos_write_raw(d, tracknr, s, 0xff56, tlk2_param);
}

static void tlk_dos_2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    tlk_dos_read_raw(d, tracknr, tbuf, 0xff56, tlk2_param);
}

struct track_handler tlk_dos_2_handler = {
    .bytes_per_sector = 6292,
    .nr_sectors = 1,
    .write_raw = tlk_dos_2_write_raw,
    .read_raw = tlk_dos_2_read_raw
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
