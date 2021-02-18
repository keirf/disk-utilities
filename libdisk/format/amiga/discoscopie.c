/*
 * disk/discoscopie.c
 * 
 * Custom format as used on DiscoScopie by ESAT Software.
 * 
 * Note the 8080808080 pattern is difficult to write to disk for sonme modern
 * PC drives. Tested OK on TEAC FD-235F and TEAC FD-235HF.
 * 
 * Written in 2021 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* Taken from Discoscopie code */
static uint16_t key[] = {
    0xaaaa, 0xaaaa, 0xaaaa, 0x9a9a, 0x9a9a, 0x8a8a, 0x8a8a, 0x8484,
    0x8484, 0x8282, 0x8282, 0x8282, 0x8282, 0x8080, 0x8080, 0x80aa,
    0xaaaa, 0xaaaa, 0xaaaa, 0x9a9a, 0x9a9a, 0x8a8a, 0x8a8a, 0x8484
};    

static void *discoscopie_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint16_t raw[24];
        unsigned int i;

        if ((uint16_t)s->word != 0x84aa)
            continue;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            goto fail;

        for (i = 0; i < ARRAY_SIZE(raw); i++)
            if (be16toh(raw[i]) != key[i])
                break;
        if (i != ARRAY_SIZE(raw))
            continue;

        ti->len = 0;
        ti->data_bitoff = 1000;
        ti->total_bits = 100000;
        return memalloc(0);
    }

fail:
    return NULL;
}

static void discoscopie_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x84aa);
    for (i = 0; i < ARRAY_SIZE(key); i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, key[i]);
}

struct track_handler discoscopie_handler = {
    .write_raw = discoscopie_write_raw,
    .read_raw = discoscopie_read_raw
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
