/*
 * disk/ak_avalon_protection.c
 *
 * Custom protection format as used on the following AK Avalon Games.
 * 
 *   Rajd Przez Polske
 *   Skaut
 *   Lowca Glow
 *   Eskadra
 *
 * Written in 2023/24 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x4425 :: Sync
 *  u32 dat[ti->len*2]
 *
 * The checksum is calculated against the raw data by summing
 * each uint32_t of raw data and if there is carry set add 1 to 
 * the sum. The actual checksum is not stored on disk, but rather
 * in code. The re-encoded data does not create the same raw 
 * data and the checksum in game will fail. 
 *
 * The protection itself decodes the first uint32_t using even
 * odd decoding. Then adds x2ed8 to the start off the raw buffer
 * plus 8 bytes for the first decode value and decodes the 
 * next 10 uint32_t's using even odd decoding. The check is done
 * at the start of the 3rd play.
 * 
 * TRKTYP_rajd_przez_polske data layout:
 *  u8 sector_data[6246*2]
 * 
 * TRKTYP_lowca_glow data layout:
 *  u8 sector_data[6246*2]
 * 
 * TRKTYP_eskadra data layout:
 *  u8 sector_data[6246*2]
 * 
 * TRKTYP_skaut_handler data layout:
 *  u8 sector_data[6246*2]
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static uint32_t checksum(uint32_t raw, uint32_t chk)
{
    uint64_t sum = 0;
    sum = (uint64_t)raw + (uint64_t)chk;

    if ((sum >> 32) > 0)
        sum++;
    return (uint32_t)sum;
}

struct ak_avalon_info {
    uint32_t checksum;
    uint32_t checksum_addition;
};

static void *ak_avalon_protection_2_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ak_avalon_info *info = handlers[ti->type]->extra_data;

    while (stream_next_bit(s) != -1) {
        uint32_t raw[ti->len/4], sum;
        unsigned int i;
        char *block;

        if (s->word != 0xaaaa4425)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        sum = 0;
        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            raw[i] = s->word;
            sum = checksum(s->word, sum);
        }
        
        if (sum+info->checksum_addition != info->checksum)
            continue;

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, raw, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

fail:
    return NULL;
}

static void ak_avalon_protection_2_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xaaaa4425);
    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, dat[i]);
    }
}

struct track_handler rajd_przez_polske_handler = {
    .bytes_per_sector = 6246*2,
    .nr_sectors = 1,
    .write_raw = ak_avalon_protection_2_write_raw,
    .read_raw = ak_avalon_protection_2_read_raw,
    .extra_data = & (struct ak_avalon_info) {
        .checksum = 0xffb26ee4,
        .checksum_addition = 8
    }
};

struct track_handler lowca_glow_handler = {
    .bytes_per_sector = 6246*2,
    .nr_sectors = 1,
    .write_raw = ak_avalon_protection_2_write_raw,
    .read_raw = ak_avalon_protection_2_read_raw,
    .extra_data = & (struct ak_avalon_info) {
        .checksum = 0x02a21036,
        .checksum_addition = 5
    }
};

struct track_handler eskadra_handler = {
    .bytes_per_sector = 6246*2,
    .nr_sectors = 1,
    .write_raw = ak_avalon_protection_2_write_raw,
    .read_raw = ak_avalon_protection_2_read_raw,
    .extra_data = & (struct ak_avalon_info) {
        .checksum = 0x11bf4e72,
        .checksum_addition = 6
    }
};

struct track_handler skaut_handler = {
    .bytes_per_sector = 6246*2,
    .nr_sectors = 1,
    .write_raw = ak_avalon_protection_2_write_raw,
    .read_raw = ak_avalon_protection_2_read_raw,
    .extra_data = & (struct ak_avalon_info) {
        .checksum = 0xab5de67a,
        .checksum_addition = 4
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
