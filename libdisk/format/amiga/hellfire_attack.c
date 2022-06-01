/*
 * disk/hellfire_attack.c
 * 
 * AmigaDOS-based long-track protection, used on Hellfire Attack by Martech.
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * Track is ~105813 bits.
 * 
 *  u16 0xa145   :: Sync
 *  u16 data[18] :: bc_mfm
 *  u16 0x5555   :: padding
 * 
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *hellfire_attack_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    uint8_t dat[ti->len/2];
    uint16_t sum;
    unsigned int i;


    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != 0xa145)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < sizeof(dat); i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            dat[i] = mfm_decode_word((uint16_t)s->word);
            sum += (uint16_t)s->word;
        }

        if (stream_next_bits(s, 16) == -1)
            break;
        if ((uint16_t)s->word != 0x5555)
            continue;

        /* custom checksum */
        if (sum != 0x5be1)
            continue;;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }
fail:
    return NULL;
}

static void hellfire_attack_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0xa145);
    for (i = 0; i < ti->len/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5555);

}

struct track_handler hellfire_attack_handler = {
    .bytes_per_sector = 36,
    .nr_sectors = 1,
    .write_raw = hellfire_attack_write_raw,
    .read_raw = hellfire_attack_read_raw
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
