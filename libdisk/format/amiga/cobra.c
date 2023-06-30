/*
 * disk/cobra.c
 *
 * Custom format as used on Cobra by Bytec
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x2195 Sync
 *  u32 0xaaaaaaaa
 *  u32 dat[ti->len/4]
 *  u32 0xaaaaaaaa
 *  u16 0x4489
 *
 * No checksums found
 *
 * TRKTYP_cobra data layout:
 *  u8 sector_data[5120]
 * 

 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint16_t crcs[];

static void *cobra_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4];
        char *block;
    
        /* sync */
        if ((uint16_t)s->word != 0x2195)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        stream_start_crc(s);
        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        /* data */
        if (stream_next_bytes(s, dat, 2*ti->bytes_per_sector) == -1)
            break;
        mfm_decode_bytes(bc_mfm_odd_even, ti->bytes_per_sector, dat, dat);

        /* padding */
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0xaaaaaaaa)
            continue;

        /* end sync */
        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x4489)
            continue;


        if(crcs[tracknr] != s->crc16_ccitt)
            trk_warn(ti, tracknr, "The checksum did not match, but may be a different version!");

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

static void cobra_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2195);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd_even, ti->len, dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaaaaaa);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
}

struct track_handler cobra_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = cobra_write_raw,
    .read_raw = cobra_read_raw
};

static const uint16_t crcs[] = {
    0x0000, 0x13e0, 0xf3e2, 0x1a19, 0x1bff, 0x36d8, 0x8646, 0xe8b4, 
    0x888d, 0x0186, 0x1355, 0x235a, 0x1159, 0xf911, 0x2b81, 0xfd77, 
    0x558d, 0xf99f, 0xfefc, 0x2739, 0x3d9b, 0xda9e, 0x26d2, 0xff81, 
    0xf1cc, 0x4082, 0xda0d, 0xc1ce, 0x0334, 0x4ab0, 0x1fd5, 0x814d, 
    0x884c, 0xf02f, 0x2682, 0xdab2, 0x93ab, 0x9ec8, 0x787c, 0x5bcd, 
    0xe433, 0x7cb7, 0x9907, 0x7e7f, 0xecca, 0x3a50, 0xe449, 0xa98d, 
    0x24fc, 0x7ee4, 0xbfe3, 0x15bf, 0x9927, 0x2c65, 0x63ef, 0xec5a, 
    0xfa97, 0xc0c3, 0x5212, 0x4cb4, 0xc69c, 0x5380, 0xafcc, 0x1f86, 
    0x34aa, 0x17c5, 0x2a20, 0x9a93, 0xe2d1, 0x183d, 0x627c, 0x6ba0, 
    0x93d1, 0xdf30, 0xcde4, 0x6590, 0x34aa, 0x179c, 0xd83c, 0xf203, 
    0x0000, 0x0000, 0x9dd3, 0x0f19, 0x3471, 0xfe7a, 0x7857, 0xfd7a, 
    0xe92a, 0x3f13, 0xc5ac, 0x614a, 0x2ea4, 0xe199, 0x9eea, 0x3b31, 
    0xb47d, 0xbfb7, 0x6252, 0xba03, 0xe30d, 0x5102, 0xd31f, 0x840a, 
    0xa552, 0xc4bd, 0x995a, 0x2544, 0xaec1, 0x8443, 0x0810, 0x6228, 
    0xb541, 0xaba7, 0xc98b, 0xedf2, 0xb9be, 0xff3a, 0xa49c, 0x2875, 
    0x7a24, 0x3aee, 0x0b74, 0x192b, 0xb8b0, 0xbe22, 0xbf32, 0x50cb, 
    0xfbfa, 0xf15d, 0x5235, 0xb2bb, 0xdc42, 0xc255, 0xd8bd, 0x7e29, 
    0xdb28, 0xd8c8, 0x23bd, 0x8fc3, 0x155c, 0xd0c6, 0x9cd0, 0xc6b8, 
    0xd367, 0xe471, 0x1bc8, 0x81d2, 0x7d3f, 0x2b3e, 0x23bd, 0x8fc3, 
    0x155c, 0xd0c6, 0x7d65, 0x3ff3, 0x47d3, 0x3d79, 0xbdbf, 0x2234
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
