/*
 * libdisk/container_mfm.c
 *
 * Write raw MFM images for tnt23 floppy emulator.
 *
 * Written in 2018 by Serge Vakulenko
 *
 * MFM file contains 160 tracks of raw MFM-encoded data.
 * Every track occupies 12800 bytes.
 * Total file length is 2048000 bytes.
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <unistd.h>

static struct container *mfm_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static void mfm_close(struct disk *d)
{
    unsigned int i, f;
    struct disk_info *di = d->di;

    if (di->nr_tracks < 160) {
        warnx("Warning: MFM file contains only %d tracks", di->nr_tracks);
    }

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    /* Only 160 tracks are supported. */
    for (i = 0; i < di->nr_tracks && i < 160; i++) {
        struct track_info *ti = &di->track[i];
        const unsigned char *data = ti->dat + ti->len*2/3;
        unsigned nbytes = ti->len/3;

        if (ti->type != TRKTYP_raw_dd) {
            warnx("Only raw_dd tracks can be written to MFM files");
            errx(1, "Please use '-f raw_dd' option");
        }

        if (nbytes > 12800) {
            nbytes = 12800;
        } else if (nbytes == 0) {
            data = (unsigned char*) "\x55";
            nbytes = 1;
        }

        /* Write raw MFM data. */
        write_exact(d->fd, data, nbytes);

        /* Repeat last byte to fill the track length. */
        for (f = nbytes; f < 12800; f++) {
            write_exact(d->fd, &data[nbytes-1], 1);
        }
    }
}

struct container container_mfm = {
    .init = dsk_init,
    .open = mfm_open,
    .close = mfm_close,
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
