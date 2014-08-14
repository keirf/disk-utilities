/*
 * disk/albedo.c
 * 
 * Custom format as used on Albedo by Myriad/Loriciels.
 * 
 * Written in 2014 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 * Sector 1:
 *  u16 0x448a,0x4aa2,0x5555 :: Sync
 *  u8 even[0x58e] :: Even
 *  u8 odd[0x58e] :: Odd
 * Sector 2:
 *  u16 0x8945,0x4a25,0x2555 :: Sync
 *  u8 even[0x378] :: Even
 *  u8 odd[0x378] :: Odd
 * Sector 3:
 *  u16 0xa291,0x4a25,0x5255 :: Sync
 *  u8 even[0x2a8] :: Even
 *  u8 odd[0x2a8] :: Odd
 * Sector 4:
 *  u16 0x8891,0x4a25,0x5525 :: Sync
 *  u8 even[0x666] :: Even
 *  u8 odd[0x666] :: Odd
 * 
 * TRKTYP_albedo data layout:
 *  u8 sector_data[0x58e]
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct protection_sector {
    uint16_t sync;
    uint32_t signature;
    unsigned int data_bytes;
};

static const struct protection_sector protection_sectors[] = {
    { 0x448a, 0x4aa25555, 0x58e },
    { 0x8945, 0x4a252555, 0x378 },
    { 0xa291, 0x4a255255, 0x2a8 },
    { 0x8891, 0x4a255525, 0x666 }
};

static void *albedo_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block = memalloc(ti->len);
    uint8_t to_see = ARRAY_SIZE(protection_sectors), seen = 0;

    while (to_see && (stream_next_bit(s) != -1)) {

        uint8_t raw[0x666*2];
        unsigned int i, sec, sec_off, sec_len;

        for (sec = 0; sec < ARRAY_SIZE(protection_sectors); sec++)
            if ((uint16_t)s->word == protection_sectors[sec].sync)
                break;
        if ((sec >= ARRAY_SIZE(protection_sectors)) || (seen & (1u<<sec)))
            continue;

        if (sec == 0)
            ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != protection_sectors[sec].signature)
            continue;

        for (i = sec_off = 0; i < sec; i++)
            sec_off += protection_sectors[i].data_bytes;
        sec_len = protection_sectors[sec].data_bytes;

        if (stream_next_bytes(s, raw, sec_len*2) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, sec_len, raw, &block[sec_off]);

        seen |= 1u<<sec;
        to_see--;
    }

    if (!to_see) {
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    free(block);
    return NULL;
}

static void albedo_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int sec;

    for (sec = 0; sec < ARRAY_SIZE(protection_sectors); sec++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16,
                  protection_sectors[sec].sync);
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32,
                  protection_sectors[sec].signature);
        tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd,
                   protection_sectors[sec].data_bytes, dat);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
        dat += protection_sectors[sec].data_bytes;
    }
}

struct track_handler albedo_protection_handler = {
    .bytes_per_sector = 0x58e + 0x378 + 0x2a8 + 0x666,
    .nr_sectors = 1,
    .write_raw = albedo_protection_write_raw,
    .read_raw = albedo_protection_read_raw
};

static void *albedo_data_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *block;

    while (stream_next_bit(s) != -1) {

        uint8_t raw[0x18c8*2];
        uint32_t csum, i;

        if ((uint16_t)s->word != 0x5122)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x91115555)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x5252aaaa)
            continue;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 0x18c8, raw, raw);

        csum = be32toh(*(uint32_t *)&raw[0x18c4]);
        for (i = 0; i < ti->len; i++)
            csum -= raw[i];
        if (csum)
            continue;

        if (stream_next_bits(s, 64) == -1)
            goto fail;
        i = ((s->word >> 7) & 0xaa) | (~s->word & 0x55);
        if (i != tracknr/2-1)
            continue;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if ((s->word & 0x7fffffff) != 0x248a248a)
            continue;

        block = memalloc(ti->len);
        memcpy(block, raw, ti->len);
        ti->total_bits = 102000;
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void albedo_data_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t i, csum;

    /* Sync and signature */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5122);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x91115555);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x5252aaaa);

    for (i = csum = 0; i < ti->len; i++)
        csum += ti->dat[i];

    /* Data and checksum */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even, 0x18c4, ti->dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even, 32, csum);
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_odd, 0x18c4, ti->dat);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd, 32, csum);

    /* MFM-illegal padding. */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, ((~csum&1)<<15) | 0x2222);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22222222);

    /* Obfuscated track #. */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, i=(tracknr/2-1)^0x55);

    /* MFM-illegal padding. */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, ((~i&1)<<31) | 0x248a248a);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x11111111);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x11111111);
}

struct track_handler albedo_data_handler = {
    .bytes_per_sector = 0x18c4,
    .nr_sectors = 1,
    .write_raw = albedo_data_write_raw,
    .read_raw = albedo_data_read_raw
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
