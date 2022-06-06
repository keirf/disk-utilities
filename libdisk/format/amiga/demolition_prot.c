/* disk/demolition_prot.c
 * 
 * AmigaDOS-based protection, used on Demolition by Anco/Kingsoft.
 * 
 * Written in 2022 by Keith Krellwitz
 *
 * TRKTYP_demolition_prot_a:
 *
 *  u16 0x4489 :: sync
 *  u16 7x 0x5544
 *  u16 0x8892
 *  u16 0x5544
 *  u16 0xAA99
 * 
 * TRKTYP_demolition_prot_b:
 *
 *  u16 0x4894 :: sync
 *  u16 7x 0x5544
 *  u16 0x8892
 *  u16 0x5544
 *  u16 0x8895
 * 
 * Both track types start with the same 7x 0x5544, 0x8892, 0x5544
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct demolition_prot_info {
    uint16_t sync;
    uint16_t key;
};


static void *demolition_prot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct demolition_prot_info *info = handlers[ti->type]->extra_data;
    unsigned int i;

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word != info->sync)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = 0; i < 7; i++) {
            if (stream_next_bits(s, 16) == -1)
                goto fail;
            if ((uint16_t)s->word != 0x5544)
                goto fail;
        }

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x8892)
            goto fail;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != 0x5544)
            goto fail;

        if (stream_next_bits(s, 16) == -1)
            goto fail;
        if ((uint16_t)s->word != info->key)
            goto fail;

        stream_next_index(s);
        ti->total_bits = s->track_len_bc;
        return memalloc(0);
    }
fail:
    return NULL;
}

static void demolition_prot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct demolition_prot_info *info = handlers[ti->type]->extra_data;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);
    for (i = 0; i < 7; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5544);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x8892);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x5544);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->key);

    for (i = 0; i < 236/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);

}

struct track_handler demolition_prot_a_handler = {
    .write_raw = demolition_prot_write_raw,
    .read_raw = demolition_prot_read_raw,
    .extra_data = & (struct demolition_prot_info) {
        .sync = 0x4489,
        .key = 0xAA99
    }
};

struct track_handler demolition_prot_b_handler = {
    .write_raw = demolition_prot_write_raw,
    .read_raw = demolition_prot_read_raw,
    .extra_data = & (struct demolition_prot_info) {
        .sync = 0x4894,
        .key = 0x8895
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
