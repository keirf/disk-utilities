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
#include <time.h>

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
#define _FLAG_normalized 3
#define _FLAG_writable   4
#define _FLAG_footer     5

struct track_header {
    uint8_t sig[3];
    uint8_t tracknr;
    uint32_t duration;
    uint32_t nr_samples;
    uint32_t offset;
};

struct footer {
    uint32_t manufacturer_offset;
    uint32_t model_offset;
    uint32_t serial_offset;
    uint32_t creator_offset;
    uint32_t application_offset;
    uint32_t comments_offset;
    uint64_t creation_time;
    uint64_t modification_time;
    uint8_t application_version;
    uint8_t hardware_version;
    uint8_t firmware_version;
    uint8_t format_revision;
    uint8_t sig[4];
};

#define SCK_NS_PER_TICK (25u)

/* Threshold beyond which we generate weak bits */
#define LONG_WEAK_THRESH (1000000u/SCK_NS_PER_TICK) /* 1000us */
#define SHORT_WEAK_THRESH (100000u/SCK_NS_PER_TICK) /* 100us */

static struct container *scp_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static void emit(uint16_t *dat, unsigned int *p_j, uint32_t cell,
                 bool_t is_weak)
{
    unsigned int j = *p_j;
    const uint32_t one_us = 1000 / SCK_NS_PER_TICK;

    /* A long pattern which transitions between 000101 and 010001. */
    if (is_weak && (cell >= LONG_WEAK_THRESH)) {
        uint32_t min = 42 * one_us/10;
        uint32_t max = 78 * one_us/10;
        uint32_t delta = 0;
        while (max*2 < cell) {
            cell -= dat[j++] = max - delta;
            cell -= dat[j++] = min + delta;
            delta += 2 * one_us/10;
            if (delta > max-min)
                delta = 0;
        }
    }

    /* A short pattern that seems to be good at losing sync: 
     * 25us, 0.5us*6, 19us, 0.5us*4 
     * The intention is to let the timing drift and weaken the eventual 
     * flux transitions by placing read pulses very close together. */
    if (is_weak && (cell >= SHORT_WEAK_THRESH)) {
        int delta = 0;
        while (32*one_us < cell) {
            delta = !delta;
            cell -= dat[j++] = (19 + delta*6) * one_us;
            for (int i = 0; i < (delta ? 6 : 4); i++)
                cell -= dat[j++] = 5*one_us/10;
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

static void checksum_and_write(
    int fd, uint32_t *p_csum, const void *dat, size_t len)
{
    uint32_t csum = *p_csum;
    const uint8_t *p = dat;
    write_exact(fd, dat, len);
    while (len--)
        csum += *p++;
    *p_csum = csum;
}

static void scp_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct disk_header dhdr;
    struct track_header thdr;
    struct footer ftr;
    struct track_raw *raw;
    unsigned int trk, i, j, bit;
    uint32_t av_cell, cell, *th_offs, file_off, csum = 0;
    uint16_t *dat, app_name_len;
    const static char app_name[] = "libdisk (keirf)";
    bool_t is_weak = FALSE;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    memset(&dhdr, 0, sizeof(dhdr));
    memcpy(dhdr.sig, "SCP", sizeof(dhdr.sig));
    dhdr.disk_type = DISKTYPE_amiga;
    dhdr.nr_revolutions = 1;
    dhdr.end_track = di->nr_tracks - 1;
    dhdr.flags = (1u<<_FLAG_index_cued)|(1u<<_FLAG_96tpi)|(1u<<_FLAG_footer);
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
        bit = raw->write_splice_bc;
        if (bit > raw->data_start_bc)
            bit = 0; /* don't mess with an already-aligned track */

        av_cell = track_nsecs_from_rpm(d->rpm) / raw->bitlen;
        j = cell = 0;

        for (i = 0; i < raw->bitlen; i++) {
            if (raw->speed[bit] == SPEED_WEAK) {
                cell += av_cell;
                is_weak = TRUE;
            } else {
                cell += (av_cell * raw->speed[bit]) / SPEED_AVG;
                if (raw->bits[bit>>3] & (0x80 >> (bit & 7))) {
                    emit(dat, &j, cell / SCK_NS_PER_TICK, is_weak);
                    cell %= SCK_NS_PER_TICK;
                    is_weak = FALSE;
                }
            }
            if (++bit >= raw->bitlen)
                bit = 0;
        }

        cell /= SCK_NS_PER_TICK;
        if (dat[0]
            && (cell < SHORT_WEAK_THRESH)
            && ((dat[0] + cell) < 0x10000u)) {
            /* Place remainder in first bitcell if the result is small. */
            dat[0] += cell;
        } else if (cell) {
            /* Place remainder in its own final bitcell. It may be too
             * significant to merge with first bitcell (eg. a weak region). */
            emit(dat, &j, cell, is_weak);
            is_weak = FALSE;
        }

        for (i = 0; i < j; i++) {
            thdr.duration += dat[i] ?: 0x10000u;
            dat[i] = htobe16(dat[i]);
        }

        checksum_and_write(d->fd, &csum, dat, j * sizeof(uint16_t));
        file_off += j * sizeof(uint16_t);

        thdr.duration = htole32(thdr.duration);
        thdr.nr_samples = htole32(j);
        lseek(d->fd, le32toh(th_offs[trk]), SEEK_SET);
        checksum_and_write(d->fd, &csum, &thdr, sizeof(thdr));
        lseek(d->fd, 0, SEEK_END);
    }

    memfree(dat);
    track_free_raw_buffer(raw);

    memset(&ftr, 0, sizeof(ftr));
    memcpy(ftr.sig, "FPCS", sizeof(ftr.sig));
    ftr.application_offset = htole32(lseek(d->fd, 0, SEEK_CUR));
    ftr.creation_time = ftr.modification_time = htole64(time(NULL));
    ftr.application_version = 0x10; /* should be moved to a general include? */
    ftr.format_revision = 0x16; /* last specification used, 1.6 */

    app_name_len = htole16(strlen(app_name));
    checksum_and_write(d->fd, &csum, &app_name_len, sizeof(app_name_len));
    checksum_and_write(d->fd, &csum, app_name, sizeof(app_name));
    checksum_and_write(d->fd, &csum, &ftr, sizeof(ftr));

    lseek(d->fd, sizeof(dhdr), SEEK_SET);
    checksum_and_write(d->fd, &csum, th_offs,
                       di->nr_tracks * sizeof(uint32_t));

    dhdr.checksum = htole32(csum);
    lseek(d->fd, 0, SEEK_SET);
    write_exact(d->fd, &dhdr, sizeof(dhdr));
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
