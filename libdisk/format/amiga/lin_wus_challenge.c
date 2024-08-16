/*
 * disk/lin_wus_challenge.c
 *
 * Custom format as used onLin Wu's Challenge by Lasersoft
 *
 * Written in 2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x2245 Sync
 *  u32 dat[ti->len/4]
 * 
 * The track data is decoded. Then the data is copied to a uint8_t
 * array to calulate the checksums for all 23 sector.  I approached
 * it this was due to the data not being aligning at the u32
 * 
 * Layout of the track
 * u16 0x2245 Sync
 * 
 * sector
 * u32 u16 sector and u16 total sector count
 * u32 checksum
 * u16
 *
 * TRKTYP_lin_wus_challenge data layout:
 *  u8 sector_data[6128]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *lin_wus_challenge_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum;
        uint8_t dat2[ti->len];
        unsigned int i,j,a;
        char *block;

        if ((uint16_t)s->word != 0x2245)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (tracknr == 1) {
            sum = 0;
            for (i = 2; i < 0x3f7+2; i++) {
                sum += be32toh(dat[i]);
            }
            if (sum != be32toh(dat[1]))
                continue;
        } else {
            memcpy(dat2,dat,ti->len);
            a=0;
            for (j = 0; j < 0x17; j++){
                a = 0x10a*j;
                sum = 0;
                for (i = 0; i < 253; i+=4) {
                    sum += (dat2[4+a+6+i] << 24) | (dat2[5+a+6+i] << 16) | (dat2[6+a+6+i] << 8) | dat2[7+a+6+i];
                }
                if (sum != ((dat2[4+a] << 24) | (dat2[5+a] << 16) | (dat2[6+a] << 8) | dat2[7+a]))
                    goto fail;
            }
        }

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

static void lin_wus_challenge_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2245);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler lin_wus_challenge_handler = {
    .bytes_per_sector = 6128,
    .nr_sectors = 1,
    .write_raw = lin_wus_challenge_write_raw,
    .read_raw = lin_wus_challenge_read_raw
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
