/*
 * disk/full_contact.c
 *
 * Custom protection format as used in Full Contact
 *
 * Written in 2022 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0xa245a245 :: sync
 *  u16 0x4489 :: sync 2
 *  u16 0x88 and sector number (0x8801,0x8802...0x880b)
 *  u32 checksum
 *  u32 data[512/4]
 * 
 * Checksum is the sum of the decoded data
 * 
 * Note: The track does not contain a sector 9
 * 
 * TRKTYP_full_contact data layout:
 *  u8 sector_data[12*512]
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *full_contact_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    unsigned int j, k, nr_valid_blocks = 0, least_block = ~0u;

    block = memalloc(ti->nr_sectors*ti->bytes_per_sector);

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint16_t hdr, raw16[2];
        uint32_t raw[2*(ti->bytes_per_sector)/4], csum, sum;
        uint32_t dat[(ti->bytes_per_sector)/4];
        unsigned int sec, i, bitoff;

        /* sync */
        if (s->word != 0xa245a245)
            continue;

        /* sync 2 */
        if (stream_next_bits(s, 16) == -1)
             break;
        if ((uint16_t)s->word != 0x4489)
            continue;
        bitoff = s->index_offset_bc - 47;

        /* header */
        if (stream_next_bytes(s, raw16, 4) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 2, raw16, &hdr);

        sec = (uint8_t)(hdr  >> 8);
        if((uint8_t)hdr!= 0x88)
            continue;

        /* checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        /* Read and decode data. */
        if (stream_next_bytes(s, raw, 2*(ti->bytes_per_sector)) == -1)
           break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->bytes_per_sector, raw, &dat);

        /* calculate cheksum */
        for (i = sum = 0; i < ti->bytes_per_sector/4; i++)
            sum += be32toh(dat[i]);

        if (sum != be32toh(csum))
            continue;

        /* check if all sectors processed or the sector has already been added*/
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        memcpy(&block[sec*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
        set_sector_valid(ti, sec);
        nr_valid_blocks++;

        if (least_block > sec) {
            ti->data_bitoff = bitoff;
            least_block = sec;
        }
    }

    /* check for missing sectors and add if missing.  Check if there are some valid
       sectors if not do not add missing sectors 
    */
    if (nr_valid_blocks > 0)
        for (j = 0; j < ti->nr_sectors; j++){
            if(!is_valid_sector(ti, j)) {
                uint32_t dat[(ti->bytes_per_sector)/4];
                for (k = 0; k < 0x80; k++) {
                    dat[k] = (int)htobe32(j*0x80+k);
                }
                memcpy(&block[j*ti->bytes_per_sector], &dat, ti->bytes_per_sector);
                set_sector_valid(ti, j);
            }
        }

    if (nr_valid_blocks == 0) {
        memfree(block);
        return NULL;
    }
    ti->total_bits = 105500;
    return block;
}

static void full_contact_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t dat[ti->bytes_per_sector/4], csum;
    unsigned int sec, i;

    for (sec = 0; sec < ti->nr_sectors; sec++) {

        memcpy(dat, &ti->dat[sec*(ti->bytes_per_sector)], ti->bytes_per_sector);
        
        /* calculate checksum */
        for (i = csum = 0; i < ti->bytes_per_sector/4; i++)
            csum += be32toh(dat[i]);

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xa245a245);

        /* sync 2 */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);

        /* 0x88 << 8 | sector */
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, (0x88 << 8) | sec);

        /* checksum*/
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->bytes_per_sector, dat);
    }
}

struct track_handler full_contact_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_raw = full_contact_write_raw,
    .read_raw = full_contact_read_raw
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
