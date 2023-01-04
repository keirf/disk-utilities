/*
 * disk/yo_joe.c
 *
 * Custom format as used on Yo! Joe! by Hudson Soft
 *
 * Written in 2023 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u16 0x2245 :: Sync & Track 1 Only 0x4489
 *  u32 dat[ti->len/4]
 * 
 * Checksum adds all decoded u32s and will be equal to 0. The
 * last decoded u32 is used only for the checksum.
 * 
 * The TRKTYP_yo_joe_b track (0.1 on disk 1) does not have a 
 * checksum and is used to store the high scores
 * 
 * TRKTYP_yo_joe_a data layout:
 *  u8 sector_data[6284]
 * 
 * TRKTYP_yo_joe_b data layout:
 *  u8 sector_data[1000]
 */

#include <libdisk/util.h>
#include <private/disk.h>

static void *yo_joe_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];

    while (stream_next_bit(s) != -1) {

        uint32_t dat[2*ti->len/4], sum;
        unsigned int i;
        char *block;

        /* sync */
        if (ti->type != TRKTYP_yo_joe_b) {
            if ((uint16_t)s->word != 0x2245)
                continue;
            ti->data_bitoff = s->index_offset_bc - 16;
        } else {
            if ((uint16_t)s->word != 0x4489)
                continue;
            ti->data_bitoff = s->index_offset_bc - 16;
        }        

        /* Read and decode data. */
        if (stream_next_bytes(s, dat, 2*ti->len) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, ti->len, dat, dat);

        /* checksum */
        for (i = sum = 0; i < ti->len/4; i++) {
            sum += be32toh(dat[i]);
        }

        /* track 1 on disk 1 does not have a checksum and the game loader for
           this track does not check it.  The caclulation is still done, but
           ignored. This is the high score track, so do not fail just give
           warning.
        */
        if (ti->type == TRKTYP_yo_joe_b && tracknr == 1) {
            if (sum != 0x1ff46176)
                trk_warn(ti, tracknr, "The high score track has been modified from the original!");
        }
        else {
            if (sum != 0)
                continue;
        }

        stream_next_index(s);
        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        ti->total_bits = s->track_len_bc;
        return block;
    }

    return NULL;
}

static void yo_joe_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    /* sync */
    if (ti->type != TRKTYP_yo_joe_b) {
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x2245);
        /* checksum */
        for (i = sum = 0; i < ti->len/4-1; i++) {
            sum += be32toh(dat[i]);
        }
        dat[ti->len/4-1] = be32toh(0-sum);
    }
    else
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, 0x4489);
 
    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, ti->len, dat);
}

struct track_handler yo_joe_a_handler = {
    .bytes_per_sector = 6284,
    .nr_sectors = 1,
    .write_raw = yo_joe_write_raw,
    .read_raw = yo_joe_read_raw
};

struct track_handler yo_joe_b_handler = {
    .bytes_per_sector = 1000,
    .nr_sectors = 1,
    .write_raw = yo_joe_write_raw,
    .read_raw = yo_joe_read_raw
};


/*
 * AmigaDOS-based long-track protection, used on Yo! Joe! by Hudson Soft.
 * 
 * Written in 2022 by Keith Krellwitz
 * 
 * Track begins with standard amigados boot, but then has a sector of 204 
 * bytes with a different sync:
 * 
 * Track is ~105500 bits. 
 *  u32 0x22452245  :: Sync
 *  u32 data[204/4] :: bc_mfm_even_odd
 * 
 * The extra sector data has its own checksum separate from the amigados
 * portion of the track
 * 
 * TRKTYP_yo_joe_boot data layout:
 *  u8 amigados[11][512]
 *  u8 extra_sector[204]
 */

static void *yo_joe_boot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block;
    uint32_t dat[2*204/4], sum;
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    while (stream_next_bit(s) != -1) {

        if (s->word != 0x22452245)
            continue;

        ti->data_bitoff = s->index_offset_bc - 31;

        /* Read and decode data. */
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(bc_mfm_even_odd, 204, dat, dat);
    
        /* checksum */
        for (i = sum = 0; i < 204/4; i++) {
            sum += be32toh(dat[i]);
        }

        if (sum != 0)
            continue;

        init_track_info(ti, TRKTYP_yo_joe_boot);
        ti->total_bits = 105500;
        block = memalloc(ti->len + sizeof(dat)/2);
        memcpy(block, ablk, ti->len);
        memcpy(&block[ti->len], dat, sizeof(dat)/2);
        ti->len += sizeof(dat)/2;
        memfree(ablk);
        return block;
    }

fail:
    memfree(ablk);
    return NULL;
}

static void yo_joe_boot_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *dat = (uint32_t *)&ti->dat[512*11], sum;
    unsigned int i;

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 32, 0x22452245);

    /* checksum */
    for (i = sum = 0; i < 204/4-1; i++) {
        sum += be32toh(dat[i]);
    }
    dat[204/4-1] = be32toh(0-sum);

    /* data */
    tbuf_bytes(tbuf, SPEED_AVG, bc_mfm_even_odd, 204, dat);   
}

struct track_handler yo_joe_boot_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = yo_joe_boot_write_raw,
    .read_raw = yo_joe_boot_read_raw
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
