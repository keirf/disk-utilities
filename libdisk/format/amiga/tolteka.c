/*
 * disk/tolteka.c
 *
 * This decoder is used by tolteka from Ariolasoft.
 *  
 * Tracks 79.0 and 79.1 contains 10 blank amigados sectors
 * and the 11th sector header, but the data of 
 * for the sector is random garbage. The trackdisk.device
 * is used to read the protection track. 
 * 
 * Four different versions where checked and the data
 * for the 11th sector was always different. The data
 * checksum in the header was always different.
 * 
 * Used amigados decoder for the 1st 10 sectors, then used
 * a custom decoder to process the 11th sector.  Basically,
 * just need to create the 11th sector with a bad data
 * checksum.
 */

#include <libdisk/util.h>
#include <private/disk.h>



static void *tolteka_protection_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    stream_reset(s);

    while (stream_next_bit(s) != -1) {
        uint32_t dat[512/4+2], raw[2];
        unsigned int i;

        /* sync */
        if (s->word != 0x44894489)
            continue;

        /* format, track, sector and gap */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);
        if(dat[0] != 0x010a9eff && dat[0] != 0x010a9fff)
            continue;

        /* label */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
 
        /* header checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[0]);

        /* data checksum */
        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[1]);

        /* data */
        for (i = 0; i < 512/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i+2]);            
        }

        /* checksums should not match for sector 11 */
        if (amigados_checksum(&dat[2], ti->bytes_per_sector) == be32toh(dat[1]))
            continue;

        init_track_info(ti, TRKTYP_tolteka_protection);
        block = memalloc(ti->len+520);
        memcpy(block, ablk, ti->len);
        memcpy(&block[ti->len], dat, 520);
        memfree(ablk);
        return block;
    }

fail:
    memfree(ablk);
    return NULL;
}

static void tolteka_protection_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)&ti->dat[512*10];
    unsigned int i;

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0);
    /* sync mark */
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    /* info */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0xff000a01 | (tracknr << 16));
    /* lbl */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, 0);
    /* header checksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[0]));
    /* data checksum */
    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[1]));
    /* data */
    for (i = 0; i < 512/4; i++)
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i+2]));
}

struct track_handler tolteka_protection_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 10,
    .write_raw = tolteka_protection_write_raw,
    .read_raw = tolteka_protection_read_raw
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
