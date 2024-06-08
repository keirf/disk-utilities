/*
 * lankhor.c
 * 
 * Custom format used on F1, Vroom, Maupiti Island, Black 
 * Sect by Lankhor & Domark.
 * 
 * Written in 2020 by Keir Fraser
 * 
 * Updated in 2023 by Keith Krellwitz - added support for
 * multidisk games and added the alt_a format to support
 * Rody & Mastico and Outzone
 * 
 * RAW TRACK LAYOUT:
 *  u16 4489
 *  u16 0x5554 only for TRKTYP_lankhor_alt_a
 *  u32 (0xfe000000 | disknr << 8) + tracknr
 *  u32 dat[0x5b5] :: even/odd
 *  u32 csum
 * 
 * Encoding is alternating even/odd, per longword.
 * Checksum is ADD.L over all decoded data longs.
 * 
 * TRKTYP_lankhor data layout:
 *  u8 sector_data[5844]
 * 
 * TRKTYP_lankhor_alt_a data layout:
 *  u8 sector_data[5640]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *lankhor_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);

    while (stream_next_bit(s) != -1) {

        uint32_t dat[ti->len/4+2], raw[2], sum, i, disknr;
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (ti->type == TRKTYP_lankhor_alt_a) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if ((uint16_t)s->word != 0x5554)
                continue;
        }

        for (i = sum = 0; i < ARRAY_SIZE(dat); i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += be32toh(dat[i]);
        }

        if ((sum != 0))
            continue;

        disknr = (uint8_t)(be32toh(dat[0]) >> 8);
        if (disktag == NULL) {
            disktag = (struct disktag_disk_nr *)
                    disk_set_tag(d, DSKTAG_disk_nr, 4, &disknr);
        }
        if (be32toh(dat[0]) != ((0xfe000000 | disknr << 8) | tracknr))
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat+1, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void lankhor_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct disktag_disk_nr *disktag = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    uint32_t *dat = (uint32_t *)ti->dat, sum, i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

    if (ti->type == TRKTYP_lankhor_alt_a)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5554);

    sum = 0xfe000000 | (disktag->disk_nr << 8) | tracknr;
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
        sum += be32toh(dat[i]);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, -sum);
}

struct track_handler lankhor_handler = {
    .bytes_per_sector = 5844,
    .nr_sectors = 1,
    .write_raw = lankhor_write_raw,
    .read_raw = lankhor_read_raw
};

struct track_handler lankhor_alt_a_handler = {
    .bytes_per_sector = 5640,
    .nr_sectors = 1,
    .write_raw = lankhor_write_raw,
    .read_raw = lankhor_read_raw
};

/*
 * Custom format used on Maupiti Island and Rody & Mastico 
 * by Lankhor.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u16 4489
 *  u32 dat[5844] :: even/odd
 * 
 * The data does not have a checksum
 */

static void *lankhor_loader_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    /* This section checks to see if the track contains the track info
       and if it does this is not the correct decoder
    */
    while (stream_next_bit(s) != -1) {
        uint32_t hdr, raw[2], disknr;

        if ((uint16_t)s->word != 0x4489)
            continue; 

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);

        if ((uint16_t)(raw[0] >> 16) == 0x5554)
            goto fail;

        disknr = (uint8_t)(be32toh(hdr) >> 8);

        if (be32toh(hdr) == ((0xfe000000 | disknr << 8) | tracknr))
            goto fail;

        break;
    }
    
    stream_reset(s);

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4];
        char *block;

        if ((uint16_t)s->word != 0x4489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        /* Read and decode data. */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void lankhor_loader_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x4489);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, ti->dat);
}

struct track_handler lankhor_loader_handler = {
    .bytes_per_sector = 5844,
    .nr_sectors = 1,
    .write_raw = lankhor_loader_write_raw,
    .read_raw = lankhor_loader_read_raw
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
