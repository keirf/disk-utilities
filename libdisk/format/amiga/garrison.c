/*
 * disk/garrison.c
 *
 * Custom format as used on Garrison by Rainbow Arts
 *
 * Written in 2022 by Keith Krellwitz.
 * 
 * The protection handler is similar to the Cyberblast decoder, so I
 * reworked the decoder similar to how Keir implemented it fr Cyberblast.
 * The downside is that there will be warning "Unexpected track length",
 * but these can be ignored.
 * 
 *
 * RAW TRACK LAYOUT:
 * 
 * TRKTYP_garrison_dir
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 dat[240/4]
 *
 * TRKTYP_garrison_data
 *  u32 0x44894489 Sync
 *  u32 0x55555555
 *  u32 checksum
 *  u32 dat[4400/4]
 * 
 * Checksum is eor'd decoded data over the fisrt 1025 u32.
 * The games decoder decodes 1100 u32 but only the first 1025 u32s
 * are used to calculate the checksum.
 * 
 * There is no checksum for the directory track
 * 
 * PROTECTION TRACKS:
 * Tracks 2 & 3 (cylinder 3) contain 4489 sync words at precise distances
 * from each other. The protection check reads 0x15fe MFM words from track 2
 * then immediately switches head (i.e. to track 3) and issues a short
 * 16-word read: this must be satisfied almost immediately (iterations of the
 * "wait for disk DMA done" loop are counted and checked).
 * 
 * The protection code checks for the longs 0x448954aa 0x54aa54aa 0x54aa4489 
 * at the beginning of track 3. Then a check of count from the disk DMA done
 * is compared to 0xC8. If the count is less than 0xC8 then success.  The next
 * check is after is done in a different secction of the code, which is no 
 * where near to the initial protection check code. A check for the value 
 * 0x55554489 proceeding the initial 3 longs is done.
 * 
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *garrison_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *block = memalloc(ti->len);

    while (stream_next_bit(s) != -1) {
        uint32_t raw[2], dat[ti->len/4], csum, sum;
        unsigned int i;

        if (((tracknr&~1) == 2) && (s->word == 0x44894489)) {
            if (stream_next_bits(s, 32) == -1)
                goto fail;
            if (tracknr == 2) {
                /* Trk 2: 0x44894489 55555555... */
                if (s->word != 0x55555555)
                    continue;
                ti->data_bitoff = 1024;
            } else {
                /* Trk 3: 0x44894489 54aa54aa 54aa54aa 44895555... */
                if (s->word != 0x54aa54aa)
                    continue;
                /* trk6 offset + trk6 read len + small offset */
                ti->data_bitoff = 1024 + 90080 + 200;
            }
            ti->total_bits = 95500;
            goto success;
        }

        if (s->word != 0x44894489)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        if (stream_next_bits(s, 32) == -1)
            goto fail;
        if (s->word != 0x55555555)
            continue;

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &csum);

        for (i = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_even_odd, 4, raw, &dat[i]);
        }

        if (ti->type == TRKTYP_garrison) {
            for (i = sum = 0; i < 1025; i++) {
                sum ^= be32toh(dat[i]);
            }

            if (be32toh(csum) != sum)
                    continue;
        }

        memcpy(block, dat, ti->len);
    success:
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;
}

static void garrison_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    if (tracknr == 2) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        for (i = 0; i < 2900; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 16, 0xffff);
        return;
    }

    if (tracknr == 3) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
        for (i = 0; i < 2; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x54aa54aa);
        for (i = 0; i < 16; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44895555);
        return;
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x44894489);
    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x55555555);

    if (ti->type == TRKTYP_garrison) {
        for (i = sum = 0; i < 1025; i++) {
            sum ^= be32toh(dat[i]);
        }
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, sum);
    }

    for (i = 0; i < ti->len/4; i++) {
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_even_odd, 32, be32toh(dat[i]));
    }
}

struct track_handler garrison_dir_handler = {
    .bytes_per_sector = 240,
    .nr_sectors = 1,
    .write_raw = garrison_write_raw,
    .read_raw = garrison_read_raw
};

struct track_handler garrison_handler = {
    .bytes_per_sector = 4400,
    .nr_sectors = 1,
    .write_raw = garrison_write_raw,
    .read_raw = garrison_read_raw
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
