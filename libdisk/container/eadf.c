/*
 * libdisk/container_eadf.c
 * 
 * Read/write Extended ADF (UAE-1ADF) images.
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/disk.h>

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

static void eadf_init(struct disk *d)
{
    _dsk_init(d, 166);
}

static struct container *eadf_open(struct disk *d)
{
    struct disk_header dhdr;
    struct track_header thdr;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, off, ext_type;

    lseek(d->fd, 0, SEEK_SET);

    read_exact(d->fd, dhdr.sig, sizeof(dhdr.sig));
    if (!strncmp(dhdr.sig, "UAE--ADF", sizeof(dhdr.sig))) {
        ext_type = 1;
        dhdr.nr_tracks = 160;
    } else if (!strncmp(dhdr.sig, "UAE-1ADF", sizeof(dhdr.sig))) {
        ext_type = 2;
        read_exact(d->fd, &dhdr.rsvd, 4);
        dhdr.nr_tracks = be16toh(dhdr.nr_tracks);
    } else {
        return NULL;
    }

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = dhdr.nr_tracks;
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ext_type == 1) {
            read_exact(d->fd, &thdr, 4);
            thdr.len = be16toh(thdr.type);
            thdr.type = !!thdr.rsvd;
            thdr.bitlen = thdr.len * 8;
        } else {
            read_exact(d->fd, &thdr, sizeof(thdr));
            thdr.type = be16toh(thdr.type);
            thdr.len = be32toh(thdr.len);
            thdr.bitlen = be32toh(thdr.bitlen);
        }
        switch (thdr.type) {
        case 0:
            if (thdr.len < 11*512) {
                warnx("Bad ADOS track len %u in Ext-ADF", ti->len);
                goto cleanup_error;
            }
            init_track_info(ti, TRKTYP_amigados);
            ti->len = thdr.len;
            ti->data_bitoff = 1024;
            ti->total_bits = DEFAULT_BITS_PER_TRACK(d);
            set_all_sectors_valid(ti);
            break;
        case 1:
            init_track_info(
                ti, thdr.bitlen ? TRKTYP_raw_dd : TRKTYP_unformatted);
            ti->len = thdr.len;
            ti->total_bits = thdr.bitlen;
            ti->data_bitoff = (ext_type == 1) ? 1024 : 0;
            break;
        default:
            warnx("Bad track type %u in Ext-ADF", thdr.type);
            goto cleanup_error;
        }
        if (ti->len == 0) {
            init_track_info(ti, TRKTYP_unformatted);
            ti->total_bits = TRK_WEAK;
        } else if ((ext_type == 1) && (ti->type == TRKTYP_raw_dd)) {
            /* RAW EXT1 tracks require the sync word to be patched in */
            ti->len += 2;
            ti->total_bits += 16;
            ti->dat = memalloc(ti->len);
            memcpy(ti->dat, &thdr.rsvd, 2);
        } else {
            ti->dat = memalloc(ti->len);
        }
    }

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        off = (ext_type == 1) && (ti->type == TRKTYP_raw_dd) ? 2 : 0;
        read_exact(d->fd, &ti->dat[off], ti->len-off);
        if (ti->type == TRKTYP_raw_dd) {
            /* Raw data has a proper marshalling API we must use. */
            struct track_info _ti = *ti;
            memset(ti, 0, sizeof(*ti));
            setup_uniform_raw_track(d, i, _ti.type, _ti.total_bits, _ti.dat);
            ti->data_bitoff = _ti.data_bitoff;
            memfree(_ti.dat);
        }
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
    memcpy(dhdr.sig, "UAE-1ADF", sizeof(dhdr.sig));
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
            for (j = 0; j < raw[i]->bitlen; j++) {
                if (raw[i]->speed[j] == 1000)
                    continue;
                fprintf(stderr, "*** T%u.%u: Variable-density track cannot be "
                        "correctly written to an Ext-ADF file\n", i/2, i&1);
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
    .init = eadf_init,
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
