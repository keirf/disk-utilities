/*
 * disk/rnc_gap.c
 * 
 * Small sectors hidden in the AmigaDOS track gap.
 * Each sector may be followed by a No Flux Area.
 * 
 * Written in 2012 by Keir Fraser
 * 
 * TRKTYP_rnc_gap data layout:
 *  u8 amigados_data[11*512]
 *  u8 rnc_signature[10]  ;; disk key/signature (same across all sectors)
 *  u8 sector_trailer_map ;; sectors which have an MFM-illegal trailer
 */

#include <libdisk/util.h>
#include <private/disk.h>

static const uint16_t sync_list[] = {
    0x8912, 0x8911, 0x8914, 0x8915 };
#define NR_SYNCS 4

unsigned int bit_weight(uint8_t *p, unsigned int nr)
{
    unsigned int nr_ones = 0;
    while (nr--) {
        uint8_t b = *p++;
        while (b) {
            if (b & 1)
                nr_ones++;
            b >>= 1;
        }
    }
    return nr_ones;
}

static void *rnc_gap_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk, *block = NULL;
    uint8_t raw[40], sig[10], sigs[4][10], nr_sigs = 0;
    uint32_t valid_blocks = 0, trailer_map = 0;
    unsigned int i, j, found, sec, nr_ones;

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto out;

    stream_reset(s);

    while ((stream_next_bit(s) != -1) && (nr_sigs < NR_SYNCS)) {

        for (sec = 0; sec < NR_SYNCS; sec++)
            if ((uint16_t)s->word == sync_list[sec])
                break;
        if ((sec == NR_SYNCS) || (valid_blocks & (1u<<sec)))
            continue;

        if (stream_next_bytes(s, raw, sizeof(raw)) == -1)
            break;
        mfm_decode_bytes(bc_mfm, sizeof(sig), raw, sig);

        /* First and last bytes of signature are static. */
        if ((sig[0] != 0xa1) || (sig[9] != 0x00))
            continue;

        /* Sector may be followed by a No Flux Area. These are mastered with 
         * a series of rapid flux transitions. Some drives read this as all 
         * ones rather than zeroes. So, just like the real protection check, 
         * we check for lots of 1s as well as lots of 0s. */
        nr_ones = bit_weight(&raw[0x18], 12);
        if ((nr_ones <= 12) || (nr_ones >= 84))
            trailer_map |= 1u << sec;

        memcpy(sigs[nr_sigs], sig, sizeof(sig));
        valid_blocks |= 1u << sec;
        nr_sigs++;
    }

    /* No matches at all? This was not an RNC-format track. */
    if (nr_sigs == 0)
        goto out;

    /* Track validation: look for two matching sectors. */
    for (i = found = 0; (i < nr_sigs) && !found; i++)
        for (j = i+1; (j < nr_sigs) && !found; j++)
            if (!memcmp(sigs[i], sigs[j], sizeof(sig)))
                found = j;
    /* If we find no matching pairs, we fail. */
    if (!found) {
        trk_warn(ti, tracknr, "Found no matching signatures "
                 "in %u sectors!", nr_sigs);
        goto out;
    }
    /* Otherwise, that's the signature we keep. */
    memcpy(sig, sigs[found], sizeof(sig));

    /* Count matches on our chosen signature. Warn on oddities. */
    for (i = found = 0; i < nr_sigs; i++)
        if (!memcmp(sigs[i], sig, sizeof(sig)))
            found++;
    if (found != nr_sigs)
        trk_warn(ti, tracknr, "Found only %u matching "
                 "signatures out of %u", found, nr_sigs);
    if (nr_sigs != NR_SYNCS)
        trk_warn(ti, tracknr, "Found only %u sectors "
               "out of %u", nr_sigs, NR_SYNCS);

    /* Build the track descriptor. */
    init_track_info(ti, TRKTYP_rnc_gap);
    ti->len += sizeof(sig) + 1;
    block = memalloc(ti->len);
    memcpy(block, ablk, 512*11);
    memcpy(&block[512*11], sig, sizeof(sig));
    block[512*11+sizeof(sig)] = trailer_map;

out:
    memfree(ablk);
    return block;
}

static void rnc_gap_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint8_t *dat = (uint8_t *)ti->dat + 512*11;
    unsigned int sec, i, trailer_map = dat[10];

    handlers[TRKTYP_amigados]->read_raw(d, tracknr, tbuf);

    for (sec = 0; sec < NR_SYNCS; sec++) {
        /* aaaa...aaaa */
        for (i = 0; i < 16; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        /* sync */
        tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, sync_list[sec]);
        /* signature */
        for (i = 0; i < 10; i++)
            tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, dat[i]);
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm, 8, 0);
        /* No Flux Area */
        if (trailer_map & (1u << sec))
            for (i = 0; i < 18; i++)
                tbuf_bits(tbuf, SPEED_AVG, bc_raw, 8, 0x00);
    }
}

struct track_handler rnc_gap_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = rnc_gap_write_raw,
    .read_raw = rnc_gap_read_raw
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
