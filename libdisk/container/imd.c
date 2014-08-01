/*
 * libdisk/container/imd.c
 * 
 * Read/write ImageDisk IMD images.
 * 
 * Written in 2014 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

struct track_header {
    uint8_t mode, cyl, head, nr_secs, sec_sz;
};

static struct container *imd_open(struct disk *d)
{
    char hdr[1024], *p;
    uint8_t secs[256], cyls[256], heads[256], c, *dat = NULL;
    struct track_header thdr;
    struct disk_info *di;
    unsigned int i, off, trk, sec_sz, type;
    off_t sz;

    sz = lseek(d->fd, 0, SEEK_END);
    lseek(d->fd, 0, SEEK_SET);

    read_exact(d->fd, hdr, sizeof(hdr));
    if (strncmp(hdr, "IMD ", 4))
        return NULL;

    for (i = 0; !(p = memchr(hdr, 0x1a, sizeof(hdr))) && (i < 5); i++)
        read_exact(d->fd, hdr, sizeof(hdr));
    if (!p) {
        warnx("IMD: Cannot find comment terminator char");
        return NULL;
    }

    off = i*sizeof(hdr) + (p-hdr) + 1;
    lseek(d->fd, off, SEEK_SET);

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = 168;
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (trk = 0; trk < di->nr_tracks; trk++) {
        struct track_info *ti = &di->track[trk];
        init_track_info(ti, TRKTYP_unformatted);
        ti->total_bits = TRK_WEAK;
    }

    while (off < sz) {
        read_exact(d->fd, &thdr, sizeof(thdr));
        off += sizeof(thdr);

        switch (thdr.mode) {
        case 0: /* 500 kbps FM */
        case 1: /* 300 kbps FM */
        case 2: /* 250 kbps FM */
            warnx("IMD: Cannot parse FM-encoded track");
            goto cleanup_error;
        case 3: /* 500 kbps MFM */
            type = TRKTYP_ibm_mfm_hd;
            break;
        case 4: /* 300 kbps MFM */
            /* Assume this is DD written on a 360 RPM drive. Fall through. */
        case 5: /* 250 kbps MFM */
            type = TRKTYP_ibm_mfm_dd;
            break;
        default:
            warnx("IMD: Unknown track mode/density 0x%02x", thdr.mode);
            goto cleanup_error;
        }

        trk = thdr.cyl*2 + (thdr.head&1);
        if (trk >= di->nr_tracks) {
            warnx("IMD: Track %u out of range", trk);
            goto cleanup_error;
        }

        if (thdr.sec_sz > 7) {
            warnx("IMD: Sector size %u out of range", thdr.sec_sz);
            goto cleanup_error;
        }
        sec_sz = 128u << thdr.sec_sz;

        read_exact(d->fd, secs, thdr.nr_secs);
        off += thdr.nr_secs;

        if (thdr.head & 0x3e) {
            warnx("IMD: Unexpected track head value 0x%02x", thdr.head);
            goto cleanup_error;
        }

        memset(cyls, thdr.cyl, thdr.nr_secs);
        if (thdr.head & 0x80) {
            read_exact(d->fd, cyls, thdr.nr_secs);
            off += thdr.nr_secs;
        }

        memset(heads, thdr.head&1, thdr.nr_secs);
        if (thdr.head & 0x40) {
            read_exact(d->fd, heads, thdr.nr_secs);
            off += thdr.nr_secs;
        }

        dat = memalloc(thdr.nr_secs * sec_sz);
        for (i = 0; i < thdr.nr_secs; i++) {
            read_exact(d->fd, &c, 1);
            off += 1;
            if (c > 8) {
                warnx("IMD: trk %u, sec %u: Bad data tag 0x%02x", trk, i, c);
                goto cleanup_error;
            }
            if (c > 4) {
                warnx("IMD: trk %u, sec %u: Data CRC error", trk, i);
                c -= 4;
            }
            if (c > 2) {
                warnx("IMD: trk %u, sec %u: Deleted-Data Address Mark "
                      "ignored", trk, i);
                c -= 2;
            }
            switch (c) {
            case 0:
                warnx("IMD: trk %u, sec %u: Sector data unavailable", trk, i);
                memset(&dat[i*sec_sz], 0, sec_sz);
                break;
            case 1:
                read_exact(d->fd, &dat[i*sec_sz], sec_sz);
                off += sec_sz;
                break;
            case 2:
                read_exact(d->fd, &c, 1);
                off += 1;
                memset(&dat[i*sec_sz], c, sec_sz);
                break;
            default:
                BUG();
            }
        }

        setup_ibm_mfm_track(d, trk, type, thdr.nr_secs, thdr.sec_sz,
                            secs, cyls, heads, dat);

        memfree(dat);
        dat = NULL;
    }

    if (off != sz) {
        warnx("IMD: Unexpected EOF");
        goto cleanup_error;
    }

    return &container_imd;

cleanup_error:
    memfree(dat);
    for (i = 0; i < di->nr_tracks; i++)
        memfree(di->track[i].dat);
    memfree(di->track);
    memfree(di);
    d->di = NULL;
    return NULL;
}

static void imd_close(struct disk *d)
{
    char timestr[30], sig[128];
    struct tm tm;
    time_t t;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    t = time(NULL);
    localtime_r(&t, &tm);
    strftime(timestr, sizeof(timestr), "%d/%m/%C%y %H:%M:%S", &tm);
    snprintf(sig, sizeof(sig),
             "IMD 1.16: %s\r\nCreated by "
             "https://github.com/keirf/Disk-Utilities\r\n\x1a",
             timestr);
    write_exact(d->fd, sig, strlen(sig));

#if 0
    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->type == TRKTYP_unformatted) {
        } else {
        }
        write_exact(d->fd, &thdr, sizeof(thdr));
    }
#endif
}

struct container container_imd = {
    .init = dsk_init,
    .open = imd_open,
    .close = imd_close,
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
