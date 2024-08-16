/*
 * disk/okay.c
 * 
 * AmigaDOS-based protection, used on King Kong and Kosmobit by Okay.
 * 
 * Written in 2023 by Keith Krellwitz
 * 
 * GCC Track is 50680 bits and is filled with a repeating pattern 
 * u8 0xBD 0xEF 0xF7 :: repeating pattern
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *okay_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    /* GCR 4us bit time */
    stream_set_density(s, 4000);
    int counter = 0;
    while (stream_next_bit(s) != -1) {
        unsigned int i, count;
        counter++;
        if ((uint16_t)s->word != 0xbdef)
            continue;

        for (i = count = 0; i < 12288/2; i++) {
            if (stream_next_bits(s, 8) == -1)
                goto fail;
            if ((uint8_t)s->word == 0xbd)
                count++;
        }

        if (count < 1000)
            continue;

        ti->total_bits = 50592;
        ti->data_bitoff = 0;
        return memalloc(0);
    }
fail:
    return NULL;

}

static void okay_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    unsigned int i;
    for (i = 0; i < ti->total_bits/8/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0xBDEFF700);
}

struct track_handler okay_protection_handler = {
    .write_raw = okay_protection_write_raw,
    .read_raw = okay_protection_read_raw
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
