/*
 * libdisk/container_dsk.c
 * 
 * Read/write DSK images.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * On-disk Format:
 *  <struct disk_header>
 *  <struct track_header> * #tracks (each entry is disk_header.bytes_per_thdr)
 *  [<struct tag_header> tag data...]+
 *  <track data...>
 * All fields are big endian (network ordering).
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

struct disk_header {
    char signature[4];
    uint16_t version;
    uint16_t nr_tracks;
    uint16_t bytes_per_thdr;
    uint16_t flags;
};

struct track_header {
    /* Enumeration */
    uint16_t type;

    uint16_t flags;

    /* Bitmap of valid sectors. */
    uint32_t valid_sectors;

    /* Offset and length of type-specific track data in container file. */
    uint32_t off;
    uint32_t len;

    /*
     * Offset from track index of raw data returned by type handler.
     * Specifically, N means that the there are N full bitcells between the
     * index pulse and the first data bitcell. Hence 0 means that the index
     * pulse occurs during the cell immediately preceding the first data cell.
     */
    uint32_t data_bitoff;

    /*
     * Total bit length of track (modulo jitter at the write splice / gap).
     * If TRK_WEAK then handler can be called repeatedly for successive
     * revolutions of the disk -- data and length may change due to 'flakey
     * bits' which confuse the disk controller.
     */
    uint32_t total_bits;
};

struct tag_header {
    uint16_t id;
    uint16_t len;
};

static void tag_swizzle(struct disk_tag *dtag)
{
    switch (dtag->id) {
    case DSKTAG_rnc_pdos_key: {
        struct rnc_pdos_key *t = (struct rnc_pdos_key *)dtag;
        t->key = ntohl(t->key);
        break;
    }
    }
}

static void dsk_init(struct disk *d)
{
    struct track_info *ti;
    struct disk_info *di;
    unsigned int i, nr_tracks = 160;

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = nr_tracks;
    di->flags = 0;
    di->track = memalloc(nr_tracks * sizeof(*ti));

    for (i = 0; i < nr_tracks; i++) {
        ti = &di->track[i];
        memset(ti, 0, sizeof(*ti));
        init_track_info(ti, TRKTYP_unformatted);
        ti->total_bits = TRK_WEAK;
    }

    d->tags = memalloc(sizeof(*d->tags));
    d->tags->tag.id = DSKTAG_end;
}

static int dsk_open(struct disk *d, bool_t quiet)
{
    struct disk_header dh;
    struct track_header th;
    struct tag_header tagh;
    struct disk_list_tag *dltag, **pprevtag;
    struct disk_tag *dtag;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, bytes_per_th, read_bytes_per_th;
    off_t off;

    read_exact(d->fd, &dh, sizeof(dh));
    if (strncmp(dh.signature, "DSK\0", 4) ||
        (ntohs(dh.version) != 0))
        return 0;

    di = memalloc(sizeof(*di));
    di->nr_tracks = ntohs(dh.nr_tracks);
    di->flags = ntohs(dh.flags);
    di->track = memalloc(di->nr_tracks * sizeof(*ti));
    read_bytes_per_th = bytes_per_th = ntohs(dh.bytes_per_thdr);
    if (read_bytes_per_th > sizeof(*ti))
        read_bytes_per_th = sizeof(*ti);

    for (i = 0; i < di->nr_tracks; i++) {
        memset(&th, 0, sizeof(th));
        read_exact(d->fd, &th, read_bytes_per_th);
        ti = &di->track[i];
        init_track_info(ti, ntohs(th.type));
        ti->flags = ntohs(th.flags);
        ti->valid_sectors = ntohl(th.valid_sectors);
        ti->len = ntohl(th.len);
        ti->data_bitoff = ntohl(th.data_bitoff);
        ti->total_bits = ntohl(th.total_bits);
        off = lseek(d->fd, bytes_per_th-read_bytes_per_th, SEEK_CUR);
        lseek(d->fd, ntohl(th.off), SEEK_SET);
        ti->dat = memalloc(ti->len);
        read_exact(d->fd, ti->dat, ti->len);
        lseek(d->fd, off, SEEK_SET);
    }

    pprevtag = &d->tags;
    do {
        read_exact(d->fd, &tagh, sizeof(tagh));
        dltag = memalloc(sizeof(*dltag) + ntohs(tagh.len));
        dtag = &dltag->tag;
        dtag->id = ntohs(tagh.id);
        dtag->len = ntohs(tagh.len);
        read_exact(d->fd, dtag+1, dtag->len);
        tag_swizzle(dtag);
        *pprevtag = dltag;
        pprevtag = &dltag->next;
    } while (dtag->id != DSKTAG_end);
    *pprevtag = NULL;

    d->di = di;
    return 1;
}

static void dsk_close(struct disk *d)
{
    struct disk_header dh;
    struct track_header th;
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct disk_list_tag *dltag;
    struct disk_tag *dtag;
    unsigned int i, datoff;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    strncpy(dh.signature, "DSK\0", 4);
    dh.version = 0;
    dh.nr_tracks = htons(di->nr_tracks);
    dh.bytes_per_thdr = htons(sizeof(th));
    dh.flags = htons(di->flags);
    write_exact(d->fd, &dh, sizeof(dh));

    datoff = sizeof(dh) + di->nr_tracks * sizeof(th);
    for (dltag = d->tags; dltag != NULL; dltag = dltag->next)
        datoff += sizeof(struct tag_header) + dltag->tag.len;

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        th.type = htons(ti->type);
        th.flags = htons(ti->flags);
        th.valid_sectors = htonl(ti->valid_sectors);
        th.off = htonl(datoff);
        th.len = htonl(ti->len);
        th.data_bitoff = htonl(ti->data_bitoff);
        th.total_bits = htonl(ti->total_bits);
        write_exact(d->fd, &th, sizeof(th));
        datoff += ti->len;
    }

    for (dltag = d->tags; dltag != NULL; dltag = dltag->next) {
        struct tag_header tagh;
        dtag = &dltag->tag;
        tagh.id = htons(dtag->id);
        tagh.len = htons(dtag->len);
        tag_swizzle(dtag);
        write_exact(d->fd, &tagh, sizeof(tagh));
        write_exact(d->fd, dtag+1, dtag->len);
        tag_swizzle(dtag);
    }

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->len != 0)
            write_exact(d->fd, ti->dat, ti->len);
    }
}

static int dsk_write_mfm(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    memset(ti, 0, sizeof(*ti));
    init_track_info(ti, type);
    ti->total_bits = DEFAULT_BITS_PER_TRACK;
    stream_reset(s, tracknr);
    stream_next_index(s);
    ti->dat = handlers[type]->write_mfm(d, tracknr, s);

    if (ti->dat == NULL) {
        memset(ti, 0, sizeof(*ti));
        init_track_info(ti, TRKTYP_unformatted);
        ti->typename = "Unformatted*";
        ti->total_bits = TRK_WEAK;
        return -1;
    }

    if (ti->total_bits == 0) {
        stream_reset(s, tracknr);
        stream_next_index(s);
        stream_next_index(s);
        ti->total_bits = s->track_bitlen ? : DEFAULT_BITS_PER_TRACK;
    }

    ti->data_bitoff = (int32_t)ti->data_bitoff % (int32_t)ti->total_bits;
    if ((int32_t)ti->data_bitoff < 0)
        ti->data_bitoff += ti->total_bits;

    return 0;
}

struct container container_dsk = {
    .init = dsk_init,
    .open = dsk_open,
    .close = dsk_close,
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
