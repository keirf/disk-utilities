/*
 * libdisk/container/scp.c
 * 
 * Write-only Supercard Pro (SCP) images.
 * 
 * Written in 2014 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

#define DISKTYPE_amiga   4

#define _FLAG_index_cued 0
#define _FLAG_96tpi      1
#define _FLAG_360rpm     2
#define _FLAG_writable   3

struct track_header {
    uint8_t sig[3];
    uint8_t tracknr;
    uint32_t duration;
    uint32_t nr_samples;
    uint32_t offset;
};

#define SCK_NS_PER_TICK (25u)

/* Threshold beyond which we generate weak bits */
#define WEAK_THRESH_NS  (100000u) /* 100us */

static struct container *scp_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static void emit(uint16_t *dat, unsigned int *p_j, uint32_t cell)
{
    unsigned int j = *p_j;

    /* Generate 3-6us weak bitcells at 100ns intervals. */
    if (cell >= WEAK_THRESH_NS/SCK_NS_PER_TICK) {
        uint32_t delta = 3000/SCK_NS_PER_TICK;
        while (2*delta < cell) {
            dat[j++] = delta;
            cell -= delta;
            delta += 100/SCK_NS_PER_TICK;
            if (delta > 6000/SCK_NS_PER_TICK)
                delta = 3000/SCK_NS_PER_TICK;
        }
    }

    /* Handle 16-bit overflow (should never happen, since we subdivide long
     * empty regions with weak bits). */
    while (cell >= 0x10000u) {
        dat[j++] = 0;
        cell -= 0x10000u;
    }

    /* Final sample: everything else; mbnz (zero is special). */
    dat[j++] = cell ?: 1;

    *p_j = j;
}

static void scp_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct disk_header dhdr;
    struct track_header thdr;
    struct track_raw *raw;
    unsigned int trk, i, j, bit;
    uint32_t av_cell, cell, *th_offs, file_off;
    uint16_t *dat;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    memset(&dhdr, 0, sizeof(dhdr));
    memcpy(dhdr.sig, "SCP", sizeof(dhdr.sig));
    dhdr.version = 0x10; /* taken from existing images */
    dhdr.disk_type = DISKTYPE_amiga;
    dhdr.nr_revolutions = 1;
    dhdr.end_track = di->nr_tracks - 1;
    dhdr.flags = (1u<<_FLAG_writable); /* avoids need for checksum */
    write_exact(d->fd, &dhdr, sizeof(dhdr));

    th_offs = memalloc(di->nr_tracks * sizeof(uint32_t));
    write_exact(d->fd, th_offs, di->nr_tracks * sizeof(uint32_t));
    file_off = sizeof(dhdr) + di->nr_tracks * sizeof(uint32_t);

    raw = track_alloc_raw_buffer(d);
    dat = memalloc(1024*1024); /* big enough */

    for (trk = 0; trk < di->nr_tracks; trk++) {

        th_offs[trk] = htole32(file_off);

        memset(&thdr, 0, sizeof(thdr));
        memcpy(thdr.sig, "TRK", sizeof(thdr.sig));
        thdr.tracknr = trk;
        thdr.offset = htole32(sizeof(thdr));
        write_exact(d->fd, &thdr, sizeof(thdr)); /* placeholder write */
        file_off += sizeof(thdr);

        track_read_raw(raw, trk);

        /* Rotate the track so gap is at index. */
        bit = max_t(int, di->track[trk].data_bitoff - 128, 0);

        av_cell = track_nsecs_from_rpm(d->rpm) / raw->bitlen;
        j = cell = 0;

        for (i = 0; i < raw->bitlen; i++) {
            if (raw->speed[bit] == SPEED_WEAK) {
                cell += av_cell;
            } else {
                cell += (av_cell * raw->speed[bit]) / SPEED_AVG;
                if (raw->bits[bit>>3] & (0x80 >> (bit & 7))) {
                    emit(dat, &j, cell / SCK_NS_PER_TICK);
                    cell %= SCK_NS_PER_TICK;
                }
            }
            if (++bit >= raw->bitlen)
                bit = 0;
        }

        cell /= SCK_NS_PER_TICK;
        if (dat[0]
            && (cell < (WEAK_THRESH_NS / SCK_NS_PER_TICK))
            && ((dat[0] + cell) < 0x10000u)) {
            /* Place remainder in first bitcell if the result is small. */
            dat[0] += cell;
        } else if (cell) {
            /* Place remainder in its own final bitcell. It may be too
             * significant to merge with first bitcell (eg. a weak region). */
            emit(dat, &j, cell);
        }

        for (i = 0; i < j; i++) {
            thdr.duration += dat[i] ?: 0x10000u;
            dat[i] = htobe16(dat[i]);
        }

        write_exact(d->fd, dat, j * sizeof(uint16_t));
        file_off += j * sizeof(uint16_t);

        thdr.duration = htole32(thdr.duration);
        thdr.nr_samples = htole32(j);
        lseek(d->fd, le32toh(th_offs[trk]), SEEK_SET);
        write_exact(d->fd, &thdr, sizeof(thdr));
        lseek(d->fd, 0, SEEK_END);
    }

    memfree(dat);
    track_free_raw_buffer(raw);

    lseek(d->fd, sizeof(dhdr), SEEK_SET);
    write_exact(d->fd, th_offs, di->nr_tracks * sizeof(uint32_t));
}

struct container container_scp = {
    .init = dsk_init,
    .open = scp_open,
    .close = scp_close,
    .write_raw = dsk_write_raw
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
