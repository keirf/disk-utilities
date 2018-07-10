/*
 * format/emu.c
 * 
 * E-mu Emulator I and II synths.
 * 
 * E-I: 35 cylinders, 1 side, tracknr = cyl
 * E-II: 80 cylinders, 2 sides, tracknr = cyl*2+side
 * CRC16: poly 0x8005, init 0x0000
 * 
 * FORMAT:
 *  Gap:    20*0xFF
 *  Header: 4*0x00, 0xFA, 0x96, (tracknr, CRC16), 00, 00[E-I], 8*0xFF, F[E-II]
 *  Data:   4*0x00, 0xFA, 0x96, (3584*data, CRC16), 2*00, 20*FF,
 *  Gap:    FF...
 * 
 * NB. All bytes are bit-swapped.
 * 
 * Written in 2018 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *emu_fm_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;

    stream_set_density(s, 3150u);

    stream_reset(s);

    while (stream_next_bit(s) != -1) {
        if (mfm_decode_word(s->word) != 0x5F69)//FA96)
            continue;
        trk_warn(ti, tracknr, "%u", s->index_offset_bc - 31);
    }

    ti->len = 3584;
    ti->total_bits = 63500;
    block = memalloc(ti->len);
    set_all_sectors_valid(ti);
    return block;
}

static void emu_fm_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xffffffff);
}

struct track_handler emui_handler = {
    .bytes_per_sector = 3584,
    .nr_sectors = 1,
    .density = trkden_single,
    .write_raw = emu_fm_write_raw,
    .read_raw = emu_fm_read_raw
};

struct track_handler emuii_handler = {
    .bytes_per_sector = 3584,
    .nr_sectors = 1,
    .density = trkden_single,
    .write_raw = emu_fm_write_raw,
    .read_raw = emu_fm_read_raw
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
