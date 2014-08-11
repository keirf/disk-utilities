/*
 * libdisk/container_img.c
 * 
 * Write IMG images (dump of logical sector contents).
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static struct container *img_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static void img_close(struct disk *d)
{
    unsigned int i;
    struct disk_info *di = d->di;
    struct track_sectors *sectors;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    sectors = track_alloc_sector_buffer(d);
    for (i = 0; i < di->nr_tracks; i++) {
        if (track_read_sectors(sectors, i) != 0)
            continue;
        write_exact(d->fd, sectors->data, sectors->nr_bytes);
    }
    track_free_sector_buffer(sectors);
}

struct container container_img = {
    .init = dsk_init,
    .open = img_open,
    .close = img_close,
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
