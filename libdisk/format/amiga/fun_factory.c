/*
 * disk/fun_factory.c
 * 
 * Custom format as used by various Fun Factory releases:
 *   Rebellion
 *   Twin Turbos
 *   Crystal Kingdom Dizzy
 *   Gadgets Lost In Time
 * 
 * The format is same as Rainbird, but the checksum follows the data block.
 * 
 * Written in 2012 by Keir Fraser
 * Gadgets Lost In Time added in 2014 by Keith Krellwitz
 * 
 * RAW TRACK LAYOUT:
 *  u32 0x44894489 :: Sync
 *  u8  0xff,0xff,0xff,trknr     : Fun Factory (usual)
 *  u8  0xff,0xff,0x00,trknr&~1  : Gadgets - Lost In Time Disk 1
 *  u8  0xff,0xff,0x01,trknr&~1  : Gadgets - Lost In Time Disk 2
 *  u32 data[10*512/4]
 *  u32 csum
 * MFM encoding of sectors:
 *  AmigaDOS style encoding and checksum (Rebellion, Twin Turbos).
 *  Gadgets - Lost In Time checksum includes the track number in
 *  the calculation
 * 
 * TRKTYP_fun_factory data layout:
 *  u8 sector_data[5120]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t gadgets_checksum(void *dat, unsigned int bytes, uint32_t hdr)
{
    uint32_t csum = hdr;
    csum ^= amigados_checksum(dat, bytes);
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    return csum;
}

static void *fun_factory_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2*ti->len/4], dat[ti->len/4], hdr, csum, trackhdr, sum;

        if (s->word != 0x44894489)
            continue;
        ti->data_bitoff = s->index_offset_bc - 31;

        if (ti->type == TRKTYP_gadgetslostintime_a)
            trackhdr = 0xffff0000u | (tracknr&~1);
        else if (ti->type == TRKTYP_gadgetslostintime_b)
            trackhdr = 0xffff0100u | (tracknr&~1);
        else
            trackhdr = 0xffffff00u | tracknr;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &hdr);
        if ((hdr = be32toh(hdr)) != trackhdr)
            continue;

        if (stream_next_bytes(s, raw, 2*ti->len) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, raw, dat);

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        sum = (ti->type == TRKTYP_fun_factory)
            ? amigados_checksum(dat, ti->len)
            : gadgets_checksum(dat, ti->len, hdr);

        if (be32toh(csum) != sum)
            continue;

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void fun_factory_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, hdr, csum;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);

    if (ti->type == TRKTYP_gadgetslostintime_a)
        hdr = 0xffff0000u | (tracknr&~1);
    else if (ti->type == TRKTYP_gadgetslostintime_b)
        hdr = 0xffff0100u | (tracknr&~1);
    else
        hdr = 0xffffff00u | tracknr;

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, hdr);

    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);

    csum = (ti->type == TRKTYP_fun_factory)
        ? amigados_checksum(dat, ti->len)
        : gadgets_checksum(dat, ti->len, hdr);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, csum);
}

struct track_handler fun_factory_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = fun_factory_write_raw,
    .read_raw = fun_factory_read_raw
};

struct track_handler gadgetslostintime_a_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = fun_factory_write_raw,
    .read_raw = fun_factory_read_raw
};

struct track_handler gadgetslostintime_b_handler = {
    .bytes_per_sector = 5120,
    .nr_sectors = 1,
    .write_raw = fun_factory_write_raw,
    .read_raw = fun_factory_read_raw
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
