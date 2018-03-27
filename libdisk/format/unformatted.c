/*
 * disk/unformatted.c
 * 
 * Unformatted (white noise) tracks.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#define SCAN_SECTOR_BITS 1000
#define SECTOR_BAD_THRESH (SCAN_SECTOR_BITS/50)
#define CLOCK_JITTER_THRESH 20 /* +/- 20% */

static void *unformatted_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    unsigned int scan_bits = 0, bad = 0, nr_zero = 0;
    unsigned int lat = s->latency, clk = s->clock;
    unsigned int bad_sectors = 0, nr_sectors = 0;

    /* Scan for bit sequences that break the MFM encoding rules.
     * Random noise will obviously do this a *lot*. */
    while (stream_next_bit(s) != -1) {
        if (s->word & 1) {
            unsigned int new_clk = (s->latency - lat) / (nr_zero + 1);
            int delta = new_clk - clk;
            if (delta < 0)
                delta = -delta;
            if (((delta*100)/clk) > CLOCK_JITTER_THRESH)
                bad++;
            clk = new_clk;
            lat = s->latency;
            nr_zero = 0;
        } else if (++nr_zero > 3) {
            bad++;
        }

        if (++scan_bits >= SCAN_SECTOR_BITS) {
            if (bad >= SECTOR_BAD_THRESH)
                bad_sectors++;
            nr_sectors++;
            bad = scan_bits = 0;
        }
    }

    if (bad_sectors < nr_sectors) {
        unsigned int pc = (bad_sectors*1000)/nr_sectors;
        if ((pc/10) <= 90)
            return NULL;
        fprintf(stderr, "*** T%u.%u: Almost certainly unformatted/empty "
                "(%u.%u%%)\n", cyl(tracknr), hd(tracknr), pc/10, pc%10);
    }

    ti->total_bits = TRK_WEAK;

    return memalloc(0); /* dummy */
}

static void unformatted_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    unsigned int i;
    int speed_delta = 200;
    uint8_t byte = 0;
    uint32_t bitlen = 96000 + (tbuf_rnd16(tbuf) & 1023) - 512;

    tbuf_init(tbuf, 0, bitlen);

    for (i = 0; i < bitlen; i++) {
        byte <<= 1;
        byte |= !(tbuf_rnd16(tbuf) & 3);
        if ((i & 7) == 7) {
            tbuf_bits(tbuf, SPEED_AVG + speed_delta, bc_raw, 8, byte);
            speed_delta = -speed_delta;
        }
    }
}

struct track_handler unformatted_handler = {
    .write_raw = unformatted_write_raw,
    .read_raw = unformatted_read_raw
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
