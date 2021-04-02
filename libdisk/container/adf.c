/*
 * libdisk/container_adf.c
 * 
 * Read/write ADF images.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void adf_init_track(struct disk *d, struct track_info *ti)
{
    init_track_info(ti, TRKTYP_amigados);
    ti->dat = memalloc(ti->len);
    ti->data_bitoff = 1024;
    ti->total_bits = DEFAULT_BITS_PER_TRACK(d);

    set_all_sectors_valid(ti);
}

static void adf_init(struct disk *d)
{
    _dsk_init(d, 160);
}

static struct container *adf_open(struct disk *d)
{
    struct track_info *ti;
    struct disk_info *di;
    unsigned int i;
    char sig[8];
    off_t sz;

    read_exact(d->fd, sig, sizeof(sig));
    if (!strncmp(sig, "UAE--ADF", sizeof(sig)) ||
        !strncmp(sig, "UAE-1ADF", sizeof(sig)))
        return container_eadf.open(d);

    sz = lseek(d->fd, 0, SEEK_END);
    if (sz != 160*512*11) {
        warnx("ADF file bad size: %lu bytes", (unsigned long)sz);
        return NULL;
    }
    lseek(d->fd, 0, SEEK_SET);

    adf_init(d);
    di = d->di;

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        adf_init_track(d, ti);
        read_exact(d->fd, ti->dat, ti->len);
    }

    return &container_adf;
}

extern void *rnc_dualformat_to_ados(struct disk *d, unsigned int tracknr);
extern void *rnc_triformat_to_ados(struct disk *d, unsigned int tracknr);
extern void *softlock_dualformat_to_ados(struct disk *d, unsigned int tracknr);

static void adf_close(struct disk *d)
{
    struct disk_info *di = d->di;
    unsigned int i, j;
    char *p;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    for (i = 0; i < di->nr_tracks; i++) {
        struct track_info *ti = &di->track[i];
        switch (ti->type) {
        case TRKTYP_amigados:
            write_exact(d->fd, di->track[i].dat, 11*512);
            break;
        case TRKTYP_amigados_extended: {
            uint8_t *p = di->track[i].dat;
            for (j = 0; j < 11; j++) {
                p += 26;
                write_exact(d->fd, p, 512);
                p += 512;
            }
            break;
        }
        case TRKTYP_rnc_dualformat:
            p = rnc_dualformat_to_ados(d, i);
            write_exact(d->fd, p, 11*512);
            memfree(p);
            break;
        case TRKTYP_rnc_triformat:
            p = rnc_triformat_to_ados(d, i);
            write_exact(d->fd, p, 11*512);
            memfree(p);
            break;
        case TRKTYP_softlock_dualformat:
            p = softlock_dualformat_to_ados(d, i);
            write_exact(d->fd, p, 11*512);
            memfree(p);
            break;
        default:
            p = memalloc(11*512);
            for (j = 0; j < 11*512/16; j++)
                memcpy(p+j*16, "-=[BAD SECTOR]=-", 16);
            write_exact(d->fd, p, 11*512);
            memfree(p);
            break;
        }
    }
}

struct container container_adf = {
    .init = adf_init,
    .open = adf_open,
    .close = adf_close,
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
