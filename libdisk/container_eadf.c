/*
 * libdisk/container_eadf.c
 * 
 * Read/write Extended ADF (UAE-1ADF) images.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

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
    di->nr_tracks = ntohs(dhdr.nr_tracks);
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        read_exact(d->fd, &thdr, sizeof(thdr));
        thdr.type = ntohs(thdr.type);
        if (thdr.type != 1) {
            warnx("Bad track type %u in Ext-ADF", thdr.type);
            goto cleanup_error;
        }
        init_track_info(ti, TRKTYP_raw);
        ti->len = ntohl(thdr.len);
        if (ti->len == 0) {
            init_track_info(ti, TRKTYP_unformatted);
            ti->total_bits = TRK_WEAK;
        } else {
            ti->dat = memalloc(ti->len);
            ti->total_bits = ntohl(thdr.bitlen);
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
    struct track_mfm *mfm[di->nr_tracks];
    unsigned int i, j;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    memset(&dhdr, 0, sizeof(dhdr));
    strncpy(dhdr.sig, "UAE-1ADF", sizeof(dhdr.sig));
    dhdr.nr_tracks = htons(di->nr_tracks);
    write_exact(d->fd, &dhdr, sizeof(dhdr));

    memset(mfm, 0, sizeof(mfm));
    thdr.type = htons(1);
    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->type == TRKTYP_unformatted) {
            thdr.len = thdr.bitlen = 0;
        } else {
            mfm[i] = track_mfm_get(d, i);
            thdr.len = htonl((mfm[i]->bitlen+7)/8);
            thdr.bitlen = htonl(mfm[i]->bitlen);
            for (j = 0; j < (mfm[i]->bitlen+7)/8; j++) {
                if (mfm[i]->speed[j] == 1000)
                    continue;
                printf("*** T%u: Variable-density track cannot be correctly "
                       "written to an Ext-ADF file\n", i);
                break;
            }
        }
        write_exact(d->fd, &thdr, sizeof(thdr));
    }

    for (i = 0; i < di->nr_tracks; i++) {
        if (mfm[i] == NULL)
            continue;
        write_exact(d->fd, mfm[i]->mfm, (mfm[i]->bitlen+7)/8);
        track_mfm_put(mfm[i]);
    }
}

struct container container_eadf = {
    .init = dsk_init,
    .open = eadf_open,
    .close = eadf_close,
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
