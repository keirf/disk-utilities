/*
 * disk/rattleheads_disk_protector.c
 * 
 * AmigaDOS-based protection for Edukacja Zestaw 3.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * Track is ~101255 bits. The track is standard amigados with data 
 * after the 11th sector:
 *  u32 0x9114AAA9 :: Data - Used to verify data
 *  u32 0x1154AAAA :: Data - Used to verify data
 *  U32 dat[58] :: Data includes the decoded longs above
 * 
 * TRKTYP_rattleheads_disk_protector data layout:
 *  u8 amigados[11][512]
 *  u8 extra_data[58*4]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct rattleheads_disk_protector_info {
    uint32_t sig1;
    uint32_t sig2;
    uint32_t dat_0_value;
    uint32_t checksum;
};

static void *rattleheads_disk_protector_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct rattleheads_disk_protector_info *info = handlers[ti->type]->extra_data;
    char *ablk, *block;
    uint32_t dat[58], raw[2], sum;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    while (stream_next_bit(s) != -1) {

        if ((s->word & 0x0fffffff) != info->sig1)
            continue;
        raw[0] = be32toh(s->word);

        if (stream_next_bits(s, 32) == -1)
            goto fail;

        if (s->word != info->sig2)
            continue;
        raw[1] = be32toh(s->word);

        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);
        if (be32toh(dat[0]) != info->dat_0_value)
            continue;

        sum = dat[0];
        for (i = 1; i < 58; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
            sum += dat[i];
        }

        if (sum != info->checksum)
            continue;

        stream_next_index(s);
        init_track_info(ti, TRKTYP_rattleheads_disk_protector);
        ti->total_bits = (s->track_len_bc/10)*10+5;
        block = memalloc(ti->len + sizeof(dat));
        memcpy(block, ablk, ti->len);
        memcpy(&block[ti->len], dat, sizeof(dat));
        ti->len += sizeof(dat);
        memfree(ablk);
        return block;
    }

fail:
    memfree(ablk);
    return NULL;
}

static void rattleheads_disk_protector_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)&ti->dat[512*11];
    unsigned int i;
 
    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    for (i = 0; i < 58; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));  
}

struct track_handler rattleheads_disk_protector_alt_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = rattleheads_disk_protector_write_raw,
    .read_raw = rattleheads_disk_protector_read_raw,
    .extra_data = & (struct rattleheads_disk_protector_info) {
        .sig1 = 0x09552AA4,
        .sig2 = 0x4952AA92,
        .dat_0_value = 0x43FA0018,
        .checksum = 0x769b8ff7
    }
};

struct track_handler rattleheads_disk_protector_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = rattleheads_disk_protector_write_raw,
    .read_raw = rattleheads_disk_protector_read_raw,
    .extra_data = & (struct rattleheads_disk_protector_info) {
        .sig1 = 0x0114AAA9,
        .sig2 = 0x1154AAAA,
        .dat_0_value = 0x337c0002,
        .checksum = 0x53170f09
    }
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
