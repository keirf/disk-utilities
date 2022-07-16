/*
 * disk/stardust.c
 *
 * Custom formats as used by Stardust and Super Stardust
 *
 * Written in 2016, 2022 by Keir Fraser
 *
 * Stardust:
 * 6 sectors of:
 *  u32 0x44894489 :: Sync
 *  u32 header {disk_nr:16, track_nr:8, sec_nr:8} :: E/O long
 *  u32 header_csum :: E/O long
 *  u32 data_csum  :: E/O long
 *  u32 dat[1032/4] :: E/O longs
 * header_csum = 'SSDT' EOR decoded header 
 * data_csum = 'SSDT' EOR all decoded data longs
 * 
 * Super Stardust:
 * 6 sectors of:
 *  u32 0x44894489 :: Sync
 *  u32 header {disk_nr:16, track_nr:8, sec_nr:8} :: E/O long
 *  u32 header_csum :: E/O long
 *  u32 dat[1032/4] :: E/O block
 *  u32 data_csum  :: E/O long
 * header_csum = 'SSDT' EOR decoded header 
 * data_csum = 'SSDT' EOR (sum of all raw mfm data longs)
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define CSUM_DUST 0x44555354
#define CSUM_SSDT 0x53534454

static void *stardust_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);
    struct disktag_disk_nr *disktag_disk_nr = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    unsigned int nr_valid_blocks = 0, least_block = ~0u;
    uint32_t exp_csum = (ti->type == TRKTYP_stardust) ? CSUM_DUST : CSUM_SSDT;

    while ((stream_next_bit(s) != -1)
           && (nr_valid_blocks != ti->nr_sectors)) {

        uint32_t bitoff, csum, sum, raw[2*1032/4], dat[1032/4];
        unsigned int i, dsk, trk, sec;

        if (s->word != 0x44894489)
            continue;
        bitoff = s->index_offset_bc - 31;

        /* Header */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);

        /* Header checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[1]);

        /* Validate the header checksum */
        csum = be32toh(dat[0]) ^ be32toh(dat[1]);
        if (csum != exp_csum)
            continue;

        dsk = be32toh(dat[0]);
        sec = dsk & 0xff;
        trk = (dsk >> 8) & 0xff;
        dsk >>= 16;

        /* Sanity-check the sector header. */
        if (!disktag_disk_nr)
            disktag_disk_nr = (struct disktag_disk_nr *)
                disk_set_tag(d, DSKTAG_disk_nr, 4, &dsk);
        if (dsk != disktag_disk_nr->disk_nr)
            continue;
        if ((trk != tracknr) || (sec >= ti->nr_sectors)
            || is_valid_sector(ti, sec))
            continue;

        if (ti->type == TRKTYP_stardust) {

            /* Stardust data checksum */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, raw);
            sum = be32toh(raw[0]);

            /* Stardust data */
            csum = 0;
            for (i = 0; i < ARRAY_SIZE(dat); i++) {
                if (stream_next_bytes(s, raw, 8) == -1)
                    goto fail;
                mfm_decode_bytes(bc_mfm_even_odd, 4, raw, dat+i);
                csum ^= be32toh(dat[i]);
            }

        } else {

            /* Super Stardust data */
            if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, sizeof(dat), raw, dat);
            csum = 0;
            for (i = 0; i < ARRAY_SIZE(raw); i++)
                csum += be32toh(raw[i]);

            /* Super Stardust data checksum */
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, raw);
            sum = be32toh(raw[0]);

        }

        /* Validate the data checksum */
        if ((csum ^ sum) != exp_csum)
            continue;

        if (sec < least_block) {
            ti->data_bitoff = bitoff - sec*2092*8;
            least_block = sec;
        }
        memcpy(&block[sec*ti->bytes_per_sector], dat, ti->bytes_per_sector);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
    }

    if (nr_valid_blocks == 0)
        goto fail;

    ti->total_bits = (ti->type == TRKTYP_stardust) ? 101300 : 103100;
    return block;

fail:
    memfree(block);
    return NULL;
}

static void stardust_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    uint32_t hdr, csum, enc_dat[2 * ti->bytes_per_sector / 4];
    unsigned int i, sec;
    struct disktag_disk_nr *disktag_disk_nr = (struct disktag_disk_nr *)
        disk_get_tag_by_id(d, DSKTAG_disk_nr);
    uint32_t exp_csum = (ti->type == TRKTYP_stardust) ? CSUM_DUST : CSUM_SSDT;

    for (sec = 0; sec < ti->nr_sectors; sec++) {
        /* Sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        /* Header */
        hdr = (disktag_disk_nr->disk_nr << 16) | (tracknr << 8) | sec;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);
        /* Header checksum */
        csum = hdr ^ exp_csum;
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
        if (ti->type == TRKTYP_stardust) {
            /* Data checksum */
            csum = exp_csum;
            for (i = 0; i < ti->bytes_per_sector/4; i++)
                csum ^= be32toh(dat[i]);
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
            /* Data */
            for (i = 0; i < ti->bytes_per_sector/4; i++)
                tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32,
                          be32toh(dat[i]));
        } else {
            /* Data */
            tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector,
                       dat);
            /* Data checksum */
            mfm_encode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, dat,
                             enc_dat, hdr);
            csum = 0;
            for (i = 0; i < ARRAY_SIZE(enc_dat); i++)
                csum += be32toh(enc_dat[i]);
            csum ^= exp_csum;
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
        }
        dat += ti->bytes_per_sector/4;
    }
}

struct track_handler stardust_handler = {
    .bytes_per_sector = 1032,
    .nr_sectors = 6,
    .write_raw = stardust_write_raw,
    .read_raw = stardust_read_raw
};

struct track_handler super_stardust_handler = {
    .bytes_per_sector = 1032,
    .nr_sectors = 6,
    .write_raw = stardust_write_raw,
    .read_raw = stardust_read_raw
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
