/*
 * disk/tech.c
 *
 * Custom format as used on Tech by Gainstar.
 *
 * Written in 2022 by Keith Krellwitz
 *
 * TRKTYP_tech RAW TRACK LAYOUT:
 *  u16 0x4891 :: Sync
 *  u32 dat[ti->len/4]
 *
 * data layout:
 *  u8 sector_data[6000]
 *
 * TRKTYP_tech_boot RAW TRACK LAYOUT:
 *  u16 0x4489 :: Sync
 *  u32 0x25a5a5a5 :: Sig
 *  u32 dat[ti->len/4]
 *
 * data layout: 
 *  u8 sector_data[4004]
 * 
 * The game does not contain checksums, so I calculated the checksums for
 * the copy I have and added a warning if the checksum does not match.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint32_t crcs[];

struct tech_info {
    uint32_t sync;
};

static void *tech_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct tech_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4];
        unsigned int i;
        char *block;

        if (s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (ti->type == TRKTYP_tech_boot) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (s->word != 0x25a5a5a5)
                continue;
        }

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (amigados_checksum(dat, ti->len) != crcs[tracknr])
            trk_warn(ti, tracknr, "The calculated checksum does not match with the" 
            " one generated during the creation of the decoder. The game may still" 
            " work fine as the loader does not have any checksum validation.");

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

static void tech_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct tech_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, info->sync);

    if (ti->type == TRKTYP_tech_boot)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x25a5a5a5);

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler tech_handler = {
    .bytes_per_sector = 6000,
    .nr_sectors = 1,
    .write_raw = tech_write_raw,
    .read_raw = tech_read_raw,
    .extra_data = & (struct tech_info) {
        .sync = 0xaaaa4891}
};

struct track_handler tech_boot_handler = {
    .bytes_per_sector = 4004,
    .nr_sectors = 1,
    .write_raw = tech_write_raw,
    .read_raw = tech_read_raw,
    .extra_data = & (struct tech_info) {
        .sync = 0xaaaa4489}
};

static const uint32_t crcs[] = {
    0x00000000, 0x55540511, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x54040414, 0x00000000, 0x51154454, 0x00000000, 0x11415144, 0x00000000,
    0x55014005, 0x00000000, 0x55050054, 0x00000000, 0x11005110, 0x00000000,
    0x01510400, 0x00000000, 0x41500101, 0x01541441, 0x10400101, 0x11150050,
    0x00000000, 0x54441041, 0x54514001, 0x41001505, 0x40541114, 0x00000000,
    0x54504501, 0x00550455, 0x04040540, 0x01101054, 0x41101540, 0x00000000,
    0x15040015, 0x00000000, 0x05155050, 0x00000000, 0x04415000, 0x00000000,
    0x44111141, 0x00000000, 0x01541044, 0x00000000, 0x55550400, 0x00000000,
    0x51545055, 0x00000000, 0x41455405, 0x00000000, 0x10001155, 0x00000000,
    0x14050101, 0x00000000, 0x05511505, 0x00000000, 0x41451411, 0x00000000,
    0x40555511, 0x00000000, 0x54155505, 0x00000000, 0x55440151, 0x00000000,
    0x51015145, 0x00000000, 0x40011445, 0x00000000, 0x15051000, 0x00000000,
    0x15140051, 0x00000000, 0x44054544, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x45054140, 0x00000000, 0x50151440, 0x00000000, 0x41055154, 0x00000000,
    0x14544405, 0x00000000, 0x01015004, 0x00000000, 0x05504540, 0x00000000,
    0x41440050, 0x00000000, 0x05015554, 0x00000000, 0x10140510, 0x00000000,
    0x40550141, 0x00000000, 0x54544400, 0x00000000, 0x50541000, 0x00000000,
    0x04150054, 0x00000000, 0x45454504, 0x00000000, 0x55401401, 0x00000000,
    0x50411115, 0x00000000, 0x45001140, 0x00000000, 0x05055154, 0x00000000,
    0x44511004, 0x00000000, 0x55041540, 0x00000000, 0x14100115, 0x00000000,
    0x55150555, 0x00000000, 0x14554111, 0x00000000, 0x00400414, 0x00000000,
    0x45545554, 0x00000000, 0x15141441, 0x00000000, 0x55100400, 0x00000000,
    0x40515151, 0x00000000, 0x01014010, 0x00000000, 0x00000000, 0x00000000,
    0x14544545, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    
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
