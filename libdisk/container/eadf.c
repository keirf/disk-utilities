/*
 * libdisk/container_eadf.c
 * 
 * Read/write Extended ADF (UAE-1ADF) images.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct disk_header {
    char sig[8];
    uint16_t rsvd, nr_tracks;
};

struct track_header {
    uint16_t rsvd, type;
    uint32_t len, bitlen;
};

static struct container *eadf_open(struct disk *d)
{
    struct disk_header dhdr;
    struct track_header thdr;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i;

    lseek(d->fd, 0, SEEK_SET);

    read_exact(d->fd, &dhdr, sizeof(dhdr));
    if (strncmp(dhdr.sig, "UAE-1ADF", sizeof(dhdr.sig)))
        return NULL;

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = be16toh(dhdr.nr_tracks);
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        read_exact(d->fd, &thdr, sizeof(thdr));
        thdr.type = be16toh(thdr.type);
        if (thdr.type != 1) {
            warnx("Bad track type %u in Ext-ADF", thdr.type);
            goto cleanup_error;
        }
        init_track_info(ti, TRKTYP_raw);
        ti->len = be32toh(thdr.len);
        if (ti->len == 0) {
            init_track_info(ti, TRKTYP_unformatted);
            ti->total_bits = TRK_WEAK;
        } else {
            ti->dat = memalloc(ti->len);
            ti->total_bits = be32toh(thdr.bitlen);
        }
    }

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        read_exact(d->fd, ti->dat, ti->len);
    }

    return &container_eadf;

cleanup_error:
    for (i = 0; i < di->nr_tracks; i++)
        memfree(di->track[i].dat);
    memfree(di->track);
    memfree(di);
    d->di = NULL;
    return NULL;
}

static void eadf_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct disk_header dhdr;
    struct track_header thdr;
    struct track_raw *raw[di->nr_tracks];
    unsigned int i, j;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    memset(&dhdr, 0, sizeof(dhdr));
    strncpy(dhdr.sig, "UAE-1ADF", sizeof(dhdr.sig));
    dhdr.nr_tracks = htobe16(di->nr_tracks);
    write_exact(d->fd, &dhdr, sizeof(dhdr));

    memset(raw, 0, sizeof(raw));
    memset(&thdr, 0, sizeof(thdr));
    thdr.type = htobe16(1);
    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->type == TRKTYP_unformatted) {
            thdr.len = thdr.bitlen = 0;
        } else {
            raw[i] = track_alloc_raw_buffer(d);
            track_read_raw(raw[i], i);
            thdr.len = htobe32((raw[i]->bitlen+7)/8);
            thdr.bitlen = htobe32(raw[i]->bitlen);
            for (j = 0; j < (raw[i]->bitlen+7)/8; j++) {
                if (raw[i]->speed[j] == 1000)
                    continue;
                printf("*** T%u: Variable-density track cannot be correctly "
                       "written to an Ext-ADF file\n", i);
                break;
            }
        }
        write_exact(d->fd, &thdr, sizeof(thdr));
    }

    for (i = 0; i < di->nr_tracks; i++) {
        if (raw[i] == NULL)
            continue;
        write_exact(d->fd, raw[i]->bits, (raw[i]->bitlen+7)/8);
        track_free_raw_buffer(raw[i]);
    }
}

struct container container_eadf = {
    .init = dsk_init,
    .open = eadf_open,
    .close = eadf_close,
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
