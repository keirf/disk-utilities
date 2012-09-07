/*
 * libdisk/container_img.c
 * 
 * Read/write IMG images (dump of IBM-MFM logical sector contents).
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define IMG_TRACKS 160

static void img_init(struct disk *d)
{
    dsk_init(d);
    d->di->nr_tracks = IMG_TRACKS;
}

static struct container *img_open(struct disk *d)
{
    struct track_info *ti;
    struct disk_info *di;
    enum track_type type = 0;
    unsigned int i;
    off_t sz;

    sz = lseek(d->fd, 0, SEEK_END);
    switch (sz) {
    case IMG_TRACKS*512*9:
        type = TRKTYP_ibm_pc_dd;
        break;
    case IMG_TRACKS*512*18:
        type = TRKTYP_ibm_pc_hd;
        break;
    case IMG_TRACKS*512*36:
        type = TRKTYP_ibm_pc_ed;
        break;
    default:
        warnx("IMG file bad size: %lu bytes", (unsigned long)sz);
        return NULL;
    }
    lseek(d->fd, 0, SEEK_SET);

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = IMG_TRACKS;
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        init_track_info(ti, type);
        set_all_sectors_valid(ti);
        ti->dat = memalloc(ti->len+1);
        ti->data_bitoff = 80 * 16; /* iam offset */
        ti->total_bits = DEFAULT_BITS_PER_TRACK;
        if (type == TRKTYP_ibm_pc_hd)
            ti->total_bits *= 2;
        else if (type == TRKTYP_ibm_pc_ed)
            ti->total_bits *= 4;
        read_exact(d->fd, ti->dat, ti->len);
        ti->dat[ti->len++] = 1; /* iam */
    }

    return &container_img;
}

static void img_close(struct disk *d)
{
    struct disk_info *di = d->di;
    enum track_type type;
    unsigned int i, trklen = 0;

    if (di->nr_tracks != IMG_TRACKS)
        errx(1, "Incorrect number of tracks to write to IMG file (%u)",
            di->nr_tracks);

    type = di->track[0].type;
    switch (type) {
    case TRKTYP_ibm_pc_dd:
        trklen = 9*512;
        break;
    case TRKTYP_ibm_pc_hd:
        trklen = 18*512;
        break;
    case TRKTYP_ibm_pc_ed:
        trklen = 36*512;
        break;
    case TRKTYP_sega_system_24:
        trklen = 5*2048 + 1024 + 256;
        break;
    default:
        errx(1, "Track 0: Unsupported track format for IMG file");
    }

    for (i = 0; i < di->nr_tracks; i++)
        if (di->track[i].type != type)
            errx(1, "Track %u: Mismatching track format for IMG file", i);

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    for (i = 0; i < di->nr_tracks; i++)
        write_exact(d->fd, di->track[i].dat, trklen);
}

struct container container_img = {
    .init = img_init,
    .open = img_open,
    .close = img_close,
    .write_mfm = dsk_write_mfm
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
