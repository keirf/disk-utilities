/*
 * disk/armourgeddon.c
 * 
 * Custom formats used only by Armour-Geddon by Psygnosis.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

/* Format A:
 *  u16 4429,5552
 *  u16 csum[2]         :: even/odd words encoding
 *  u16 data[6296/2][2] :: even/odd words encoding
 * Checksum is ADD.W based. */

static uint16_t checksum(uint16_t *dat, unsigned int nr)
{
    unsigned int i;
    uint32_t sum = -2;

    for (i = 0; i < nr; i++) {
        /* Simulate M68K ADDX instruction */
        if (sum > 0xffff)
            sum = (uint16_t)(sum+1);
        sum += be16toh(dat[i]);
    }
    sum &= 0xfffa;
    return (uint16_t)sum;
}

static void *armourgeddon_a_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block;
    uint16_t dat[0xc4d*2];
    unsigned int i;

    while (stream_next_bit(s) != -1) {
            
        if ((uint16_t)s->word != 0x4429)
            continue;
        ti->data_bitoff = s->index_offset_bc - 15;

        if (stream_next_bits(s, 16) == -1) /* 0x5552 */
            break;

        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        for (i = 0; i < 0xc4d; i++)
            mfm_decode_bytes(bc_mfm_even_odd, 2, &dat[2*i], &dat[i]);

        if (checksum(dat+1, 0xc4c) != be16toh(*dat))
            continue;

        ti->total_bits = 105500;
        set_all_sectors_valid(ti);
        block = memalloc(ti->len);
        memcpy(block, (uint16_t *)dat+1, ti->len);
        return block;
    }

    return NULL;
}

static void armourgeddon_a_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint16_t *dat = (uint16_t *)ti->dat;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4429);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0xfc);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, checksum(dat, ti->len/2));
    for (i = 0; i < ti->len/2; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 16, be16toh(dat[i]));
}

struct track_handler armourgeddon_a_handler = {
    .bytes_per_sector = 6296,
    .nr_sectors = 1,
    .write_raw = armourgeddon_a_write_raw,
    .read_raw = armourgeddon_a_read_raw
};

/* Format B:
 *  u16 4489,4489,4489,5555
 *  u8  signature[4][2] :: even/odd bytes encoding, signature is "KEEP"
 *  u8  disk_id[2]      :: even/odd bytes encoding
 *  u8  data[12*512][2] :: even/odd bytes encoding
 * No checksum or validation info on these tracks!! */

static void *armourgeddon_b_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t raw[2], dat[5+6*1024], *block;
    unsigned int i;

    while (stream_next_bit(s) != -1) {
            
        if (s->word != 0x44894489)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x44895555)
            continue;

        ti->data_bitoff = s->index_offset_bc - 63;

        for (i = 0; i < sizeof(dat); i++) {
            if (stream_next_bytes(s, raw, 2) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 1, raw, &dat[i]);
        }
        if (strncmp((char *)dat, "KEEP", 4))
            continue;

        ti->total_bits = 105500;
        set_all_sectors_valid(ti);
        block = memalloc(ti->len+1);
        memcpy(block, dat+5, ti->len);
        block[ti->len] = dat[4];
        ti->len++;
        return block;
    }

fail:
    return NULL;
}

static void armourgeddon_b_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat;
    unsigned int i, len = ti->len-1;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895555);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, 0x4b);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, 0x45);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, 0x45);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, 0x50);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[len]);

    for (i = 0; i < len; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 8, dat[i]);
}

struct track_handler armourgeddon_b_handler = {
    .bytes_per_sector = 12*512,
    .nr_sectors = 1,
    .write_raw = armourgeddon_b_write_raw,
    .read_raw = armourgeddon_b_read_raw
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
