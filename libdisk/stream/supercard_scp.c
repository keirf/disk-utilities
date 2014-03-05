/*
 * stream/supercard_scp.c
 * 
 * Parse SuperCard Pro SCP flux format.
 * 
 * Written in 2014 by Simon Owen, based on code by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_REVS  5

struct scp_stream {
    struct stream s;
    int fd;

    /* Current track number. */
    unsigned int track;

    /* Raw track data. */
    unsigned char *dat;
    unsigned int datsz;
    unsigned int filesz;

    unsigned int revs;       /* stored disk revolutions */
    unsigned int dat_idx;    /* current index into dat[] */
    unsigned int index_pos;  /* next index offset */

    unsigned int index_off[MAX_REVS]; /* data offsets of each index */
};

#define SCK_PS_PER_TICK (25000)

static struct stream *scp_open(const char *name)
{
    struct stat sbuf;
    struct scp_stream *scss;
    char header[0x10];
    int fd, filesz;

    if (stat(name, &sbuf) < 0)
        return NULL;

    if ((fd = file_open(name, O_RDONLY)) == -1)
        err(1, "%s", name);

    read_exact(fd, header, sizeof(header));

    if (((filesz = lseek(fd, 0, SEEK_END)) < 0) || (lseek(fd, 0, SEEK_SET) < 0))
        err(1, "%s", name);

    if (memcmp(header, "SCP", 3) != 0)
        errx(1, "%s is not a SCP file!", name);

    if (header[5] == 0)
        errx(1, "%s has an invalid revolution count (%u)!", name, header[5]);

    if (header[9] != 0 && header[9] != 16)
        errx(1, "%s has unsupported bit cell time width (%u)", name, header[9]);

    scss = memalloc(sizeof(*scss));
    scss->fd = fd;
    scss->filesz = filesz;
    scss->revs = min((int)header[5], MAX_REVS);

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
    unsigned int rev, trkoffset[MAX_REVS];
    uint16_t cyl, head;
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

    cyl = trk_header[3] / 2;
    head = trk_header[3] & 1;

    if (tracknr != (cyl*2)+head)
        return -1;

    for (rev = 0 ; rev < scss->revs ; rev++) {
        read_exact(scss->fd, longwords, sizeof(longwords));
        trkoffset[rev] = tdh_offset + le32toh(longwords[2]);
        scss->index_off[rev] = le32toh(longwords[1]) * sizeof(uint16_t);
        scss->datsz += scss->index_off[rev];
    }

    scss->dat = memalloc(scss->datsz);
    scss->datsz = 0;

    for (rev = 0 ; rev < scss->revs ; rev++) {
        if (lseek(scss->fd, trkoffset[rev], SEEK_SET) != trkoffset[rev])
            return -1;
        read_exact(scss->fd, scss->dat+scss->datsz, scss->index_off[rev]);
        scss->datsz += scss->index_off[rev];
        scss->index_off[rev] = scss->datsz;
    }

    scss->track = tracknr;

    return 0;
}

static void scp_reset(struct stream *s)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);

    scss->dat_idx = 0;
    scss->index_pos = ~0u;
}

static int scp_next_flux(struct stream *s)
{
    struct scp_stream *scss = container_of(s, struct scp_stream, s);
    unsigned int i = scss->dat_idx;
    unsigned char *dat = scss->dat;
    uint32_t val = 0, flux;
    bool_t done = 0;

    if ((i == 0 || i >= scss->index_pos) && s->nr_index < scss->revs) {
        scss->index_pos = scss->index_off[s->nr_index];
        index_reset(s);
    }

    while (i < scss->datsz) {
        uint16_t t = be16toh(*(uint16_t*)&dat[i]);
        i += sizeof(uint16_t);

        if (t == 0) { /* overflow */
            val += 0x10000;
            continue;
        }

        val += t;
        done = 1;
        break;
    }

    scss->dat_idx = i;

    if (!done)
        return -1;

    flux = (val * (uint32_t)SCK_PS_PER_TICK) / 1000u;
    return (int)flux;
}

struct stream_type supercard_scp = {
    .open = scp_open,
    .close = scp_close,
    .select_track = scp_select_track,
    .reset = scp_reset,
    .next_bit = flux_next_bit,
    .next_flux = scp_next_flux,
    .suffix = { "scp", NULL }
};
