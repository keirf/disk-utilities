/*
 * stream/supercard_scp.c
 * 
 * Parse SuperCard Pro SCP flux format.
 * 
 * Written in 2014 by Simon Owen, based on code by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/stream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct scp_stream {
    struct stream s;
    int fd;

    /* Current track number. */
    unsigned int track;

    /* Raw track data. */
    uint16_t *dat;
    unsigned int datsz;

    bool_t index_cued;
    unsigned int revs;       /* stored disk revolutions */
    unsigned int dat_idx;    /* current index into dat[] */
    unsigned int index_pos;  /* next index offset */
    int jitter;              /* accumulated injected jitter */

    int total_ticks;         /* total ticks to final index pulse */
    int acc_ticks;           /* accumulated ticks so far */

    unsigned int index_off[]; /* data offsets of each index */
};

struct disk_header {
    uint8_t sig[3];
    uint8_t version;
    uint8_t disk_type;
    uint8_t nr_revolutions;
    uint8_t start_track;
    uint8_t end_track;
    uint8_t flags;
    uint8_t cell_width;
    uint16_t reserved;
    uint32_t checksum;
};

#define SCK_NS_PER_TICK (25u)

static struct stream *scp_open(const char *name, unsigned int data_rpm)
{
    struct stat sbuf;
    struct scp_stream *scss;
    struct disk_header header;
    uint8_t revs;
    int fd;

    if (stat(name, &sbuf) < 0)
        return NULL;

    if ((fd = file_open(name, O_RDONLY)) == -1)
        err(1, "%s", name);

    read_exact(fd, &header, sizeof(header));

    if (memcmp(header.sig, "SCP", 3) != 0)
        errx(1, "%s is not a SCP file!", name);

    if ((revs = header.nr_revolutions) == 0)
        errx(1, "%s has an invalid revolution count (%u)!", name, revs);

    if (header.cell_width != 0 && header.cell_width != 16)
        errx(1, "%s has unsupported bit cell time width (%u)",
             name, header.cell_width);

    if (!(header.flags & (1u<<4)) && header.checksum) {
        int sz;
        uint8_t *p, *buf;
        uint32_t csum = 0;
        if ((sz = lseek(fd, 0, SEEK_END)) < 16)
            errx(1, "%s is too short", name);
        sz -= 16;
        buf = memalloc(sz);
        lseek(fd, 16, SEEK_SET);
        read_exact(fd, buf, sz);
        p = buf;
        while (sz--)
            csum += *p++;
        memfree(buf);
        if (csum != le32toh(header.checksum))
            errx(1, "%s has bad checksum", name);
        lseek(fd, 16, SEEK_SET);
    }

    scss = memalloc(sizeof(*scss) + revs*sizeof(unsigned int));
    scss->fd = fd;
    scss->revs = revs;
    scss->index_cued = !!(header.flags & (1u<<0)) || (scss->revs == 1);
    if (!scss->index_cued)
        scss->revs--;

    return &scss->s;
}

static void scp_close(struct stream *s)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);
    close(scss->fd);
    memfree(scss->dat);
    memfree(scss);
}

static int scp_select_track(struct stream *s, unsigned int tracknr)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);
    uint8_t trk_header[4];
    uint32_t longwords[3];
    unsigned int rev, trkoffset[scss->revs];
    uint32_t hdr_offset, tdh_offset;

    if (scss->dat && (scss->track == tracknr))
        return 0;

    memfree(scss->dat);
    scss->dat = NULL;
    scss->datsz = 0;
    
    hdr_offset = 0x10 + tracknr*sizeof(uint32_t);

    if (lseek(scss->fd, hdr_offset, SEEK_SET) != hdr_offset)
        return -1;

    read_exact(scss->fd, longwords, sizeof(uint32_t));
    tdh_offset = le32toh(longwords[0]);

    if (lseek(scss->fd, tdh_offset, SEEK_SET) != tdh_offset)
        return -1;

    read_exact(scss->fd, trk_header, sizeof(trk_header));
    if (memcmp(trk_header, "TRK", 3) != 0)
        return -1;

    if (trk_header[3] != tracknr)
        return -1;

    if (!scss->index_cued) {
        /* Skip first partial revolution. */
        lseek(scss->fd, 12, SEEK_CUR);
    }

    scss->total_ticks = 0;
    for (rev = 0 ; rev < scss->revs ; rev++) {
        read_exact(scss->fd, longwords, sizeof(longwords));
        trkoffset[rev] = tdh_offset + le32toh(longwords[2]);
        scss->index_off[rev] = le32toh(longwords[1]);
        scss->total_ticks += le32toh(longwords[0]);
        scss->datsz += scss->index_off[rev];
    }

    scss->dat = memalloc(scss->datsz * sizeof(scss->dat[0]));
    scss->datsz = 0;

    for (rev = 0 ; rev < scss->revs ; rev++) {
        if (lseek(scss->fd, trkoffset[rev], SEEK_SET) != trkoffset[rev])
            return -1;
        read_exact(scss->fd, &scss->dat[scss->datsz],
                   scss->index_off[rev] * sizeof(scss->dat[0]));
        scss->datsz += scss->index_off[rev];
        scss->index_off[rev] = scss->datsz;
    }

    scss->track = tracknr;

    s->max_revolutions = scss->revs + 1;
    return 0;
}

static void scp_reset(struct stream *s)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);

    scss->jitter = 0;
    scss->dat_idx = 0;
    scss->index_pos = 0;
    scss->acc_ticks = 0;
}

static int scp_next_flux(struct stream *s)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);
    uint32_t val = 0, t;
    unsigned int nr_index_seen = 0;

    for (;;) {
        if (scss->dat_idx >= scss->index_pos) {
            uint32_t rev = s->nr_index % scss->revs;
            if ((rev == 0) && (scss->index_pos != 0)) {
                /* We are wrapping back to the start of the dump. Unless a flux
                 * reversal sits exactly on the index we have some time to
                 * donate to the first reversal of the first revolution. */
                val = scss->total_ticks - scss->acc_ticks;
                scss->acc_ticks = -val;
            }
            scss->index_pos = scss->index_off[rev];
            if (rev == 0)
                scss->dat_idx = 0;
            s->ns_to_index = s->flux;
            /* Some drives return no flux transitions for tracks >= 160.
             * Bail if we see no flux transitions in a complete revolution. */
            if (nr_index_seen++)
                break;
        }

        t = be16toh(scss->dat[scss->dat_idx++]);

        if (t == 0) { /* overflow */
            val += 0x10000;
            continue;
        }

        val += t;
        break;
    }

    scss->acc_ticks += val;

    /* If we are replaying a single revolution then jitter it a little to
     * trigger weak-bit variations. */
    if (scss->revs == 1) {
        int32_t jitter = rnd16(&s->prng_seed) & 3;
        if ((scss->jitter >= 4) || (scss->jitter <= -4)) {
            /* Already accumulated significant jitter; adjust for it. */
            jitter = scss->jitter / 2;
        } else if (jitter & 1) {
            /* Add one bit of jitter. */
            jitter >>= 1;
        } else {
            /* Subtract one bit of jitter. */
            jitter >>= 1;
            jitter = -jitter;
        }
        scss->jitter -= jitter;
        val += jitter;
    }

    val = ((uint64_t)val * SCK_NS_PER_TICK * s->drive_rpm) / s->data_rpm;

    /* If we are replaying a single revolution then randomly ignore 
     * very short pulses (<1us). */
    if ((scss->revs == 1) && (val < 1000) && (rnd16(&s->prng_seed) & 1)) {
        scss->jitter += val;
        val = 0;
    }

    s->flux += val;
    return 0;
}

struct stream_type supercard_scp = {
    .open = scp_open,
    .close = scp_close,
    .select_track = scp_select_track,
    .reset = scp_reset,
    .next_flux = scp_next_flux,
    .suffix = { "scp", NULL }
};
