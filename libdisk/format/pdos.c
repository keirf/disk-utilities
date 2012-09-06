/*
 * disk/pdos.c
 * 
 * Rob Northen Computing Protected DOS (RNC PDOS) format.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x1448     :: MFM sync
 *  12 back-to-back sectors, 0x40a MFM bytes each
 * Decoded sector:
 *  u8 --          :: sector marker MFM sync 0x4891
 *  u8 sector      :: 0-11
 *  u8 track       :: 0-159
 *  u16 csum
 *  u8 data[512]
 *  u8 gap_words   :: usually 28(decimal), 0 on last sector
 * MFM encoding of sectors:
 *  <sector,track,csum>_even, <sector,track,csum>_odd
 *  <data>_even, <data>_odd
 *  Header long is EORed with disk key with bit 31 set. 
 *  Data longs are chain EORed with the disk key
 * Timings:
 *  Long track (~105500 bits), uniform density.
 * 
 * TRKTYP_pdos data layout:
 *  u8 sector_data[12][512]
 */

#include <libdisk/util.h>
#include "../private.h"

#include <arpa/inet.h>

static void *pdos_write_mfm(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(512 * ti->nr_sectors);
    unsigned int i, j, nr_valid_blocks = 0;
    struct rnc_pdos_key *keytag = (struct rnc_pdos_key *)
        disk_get_tag_by_id(d, DSKTAG_rnc_pdos_key);

    while (stream_next_bit(s) != -1) {

        uint8_t hdr[2*4], dat[2*512], skip;
        uint32_t k, *p, *q, csum;

        if ((uint16_t)s->word != 0x1448)
            continue;
        ti->data_bitoff = s->index_offset - 15;

        for (i = 0; i < ti->nr_sectors; i++) {
            /* Check per-sector sync. */
            if (stream_next_bits(s, 16) == -1)
                goto done;
            if ((uint16_t)s->word != 0x4891)
                break;

            /* Read and MFM-decode the header and data areas. */
            if ((stream_next_bytes(s, hdr, 2*4) == -1) ||
                (stream_next_bytes(s, dat, 2*512) == -1))
                goto done;
            mfm_decode_bytes(MFM_even_odd, 4, hdr, hdr);
            mfm_decode_bytes(MFM_even_odd, 512, dat, dat);
            csum = amigados_checksum(dat, 512);
            csum = (uint16_t)(csum | (csum >> 15));

            if (keytag == NULL) {
                /* Brute-force the key. */
                uint32_t key = ((hdr[0] ^ i) & 0x7f) << 24;
                key |= (hdr[1] ^ tracknr) << 16;
                key |= (hdr[2] ^ (uint8_t)(csum>>8)) << 8;
                key |= hdr[3] ^ (uint8_t)csum;
                keytag = (struct rnc_pdos_key *)
                    disk_set_tag(d, DSKTAG_rnc_pdos_key, 4, &key);
            } else {
                *(uint32_t *)hdr ^= ntohl(keytag->key) ^ 0x80;
                if ((hdr[0] != i) || (hdr[1] != tracknr) ||
                    (hdr[2] != (uint8_t)(csum>>8)) ||
                    (hdr[3] != (uint8_t)csum))
                    break;
            }

            /* Decrypt and stash the data block. */
            k = keytag->key;
            p = (uint32_t *)dat;
            q = (uint32_t *)&block[i*512];
            for (j = 0; j < 512/4; j++) {
                uint32_t enc = ntohl(*p++);
                *q++ = htonl(enc ^ k);
                k = enc;
            }

            /* Skip the sector gap. */
            if (stream_next_bits(s, 16) == -1)
                goto done;
            skip = mfm_decode_bits(MFM_all, (uint16_t)s->word);
            if (stream_next_bits(s, skip*16) == -1)
                goto done;
        }

        if (i == ti->nr_sectors) {
            nr_valid_blocks = ti->nr_sectors;
            break;
        }
    }

done:
    if (nr_valid_blocks == 0) {
        free(block);
        return NULL;
    }

    ti->total_bits = 105500;
    set_all_sectors_valid(ti);

    return block;
}

static void pdos_read_mfm(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t k, *dat = (uint32_t *)ti->dat;
    unsigned int i, j;
    struct rnc_pdos_key *keytag = (struct rnc_pdos_key *)
        disk_get_tag_by_id(d, DSKTAG_rnc_pdos_key);

    tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x1448);

    for (i = 0; i < ti->nr_sectors; i++) {

        uint32_t hdr = (i << 24) | (tracknr << 16);
        uint32_t csum, enc[128];

        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, MFM_raw, 16, 0x4891);

        /* encrypt data */
        k = keytag->key;
        for (j = 0; j < 128; j++) {
            k ^= ntohl(*dat++);
            enc[j] = htonl(k);
        }

        /* header */
        csum = amigados_checksum(enc, 512);
        if (!is_valid_sector(ti, i))
            csum ^= 1; /* bad checksum for an invalid sector */
        hdr |= (csum & 0x5555u) | ((csum >> 15) & 0xaaaau);
        hdr ^= keytag->key ^ (1u<<31);
        tbuf_bits(tbuf, SPEED_AVG, MFM_even_odd, 32, hdr);

        /* data */
        tbuf_bytes(tbuf, SPEED_AVG, MFM_even_odd, 512, enc);

        /* gap */
        tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8,
                  i == (ti->nr_sectors - 1) ? 0 : 28);
        for (j = 0; j < 28; j++)
            tbuf_bits(tbuf, SPEED_AVG, MFM_all, 8, 0);
    }
}

struct track_handler rnc_pdos_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 12,
    .write_mfm = pdos_write_mfm,
    .read_mfm = pdos_read_mfm
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
