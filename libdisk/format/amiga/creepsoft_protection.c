/*
 * disk/creepsoft_protection.c
 *
 *  This protection is used by the following Creepsoft games:
 *  
 *   - Astro Marine Corps (A.M.C.)
 *   - Mega Phoenix
 *   - Satan - Retail
 *  
 * First a check is done for the decoded key 1 and stores the address 
 * and then a check is done for the decoded key 2 and stores 
 * the address. Then 0x1960 is added to the address of the key 1.
 * The address of key 2 is then subtracted from this and finally
 * subtract 0x15d0 from this. The result needs to be between 
 * 0xfffffffc and 4.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *creepsoft_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)mfm_decode_word(s->word) != 0x4078)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if ((uint16_t)mfm_decode_word(s->word) != 0x9080)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if ((uint16_t)mfm_decode_word(s->word) == 0x8090)
            break;
    }

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)mfm_decode_word(s->word) != 0xa1fe)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if ((uint16_t)mfm_decode_word(s->word) != 0x5000)
            continue;
        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if ((uint16_t)mfm_decode_word(s->word) != 0x2103)
            continue;

        ti->data_bitoff = 0;
        ti->total_bits = 101500;
        return memalloc(0);
    }
fail:
    return NULL;
}

static void creepsoft_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    for (i = 0; i < 500; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
    /* key 1 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x4078);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x9080);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x8090);
    for (i = 0; i < 226; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 32, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
    /* key 2 */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xa1a1);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xa1fe);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x5000);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0x2103);
}

struct track_handler creepsoft_protection_handler = {
    .write_raw = creepsoft_protection_write_raw,
    .read_raw = creepsoft_protection_read_raw
};

/*
 *
 * This is an alternate version of the creepsoft protection
 * which is used by another possibly budget release of 
 * Astro Marine Corps (A.M.C.)
 *  
 * The protection code is still in place and does check
 * track 79.0, but always fails. Normally on success the 
 * long 0x54455441 is written to 0x37d7c.  On failer the 
 * address is cleared. The check for the value at this 
 * address has been modified to always pass.
 * 
 * The track can contain anything as long as it has a
 * sync of 0x4489. The correct protection track is 
 * written.
 * 
 */
static void *creepsoft_protection_alt_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    if (tracknr != 158)
        goto fail;


    while (stream_next_bit(s) != -1) {

        if ((uint16_t)s->word == 0x4489)
            break;  
    }

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)mfm_decode_word(s->word) == 0x4078)
            break;
    }

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)mfm_decode_word(s->word) == 0x9080)
            break;
    }

    while (stream_next_bit(s) != -1) {

        if ((uint16_t)mfm_decode_word(s->word) != 0x8090)
            continue;

        stream_next_index(s);
        ti->data_bitoff = 0;
        ti->total_bits = (s->track_len_bc/100)*100+100;
        return memalloc(0);
    }

fail:
    return NULL;
}

struct track_handler creepsoft_protection_alt_handler = {
    .write_raw = creepsoft_protection_alt_write_raw,
    .read_raw = creepsoft_protection_read_raw
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
