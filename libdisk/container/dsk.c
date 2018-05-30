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
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

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
    uint16_t nr_sectors;
    uint16_t bytes_per_sector;
    uint8_t valid_sectors[8];

    /* Offset and length of type-specific track data in container file. */
    uint32_t off;
    uint32_t len;

    /* Offset from track index of raw data returned by type handler.
     * Specifically, N means that the there are N full bitcells between the
     * index pulse and the first data bitcell. Hence 0 means that the index
     * pulse occurs during the cell immediately preceding the first data
     * cell. */
    uint32_t data_bitoff;

    /* Total bit length of track (modulo jitter at the write splice / gap). If 
     * TRK_WEAK then handler can be called repeatedly for successive
     * revolutions of the disk -- data and length may change due to 'flakey
     * bits' which confuse the disk controller. */
    uint32_t total_bits;
};

struct tag_header {
    uint16_t id;
    uint16_t len;
};

static void tag_swizzle(struct disktag *dtag)
{
    switch (dtag->id) {
    case DSKTAG_rnc_pdos_key: {
        struct disktag_rnc_pdos_key *t = (struct disktag_rnc_pdos_key *)dtag;
        t->key = be32toh(t->key);
        break;
    }
    case DSKTAG_disk_nr: {
        struct disktag_disk_nr *t = (struct disktag_disk_nr *)dtag;
        t->disk_nr = be32toh(t->disk_nr);
        break;
    }
    }
}

void _dsk_init(struct disk *d, unsigned int nr_tracks)
{
    struct track_info *ti;
    struct disk_info *di;
    unsigned int i;

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = nr_tracks;
    di->flags = 0;
    di->track = memalloc(nr_tracks * sizeof(*ti));

    for (i = 0; i < nr_tracks; i++)
        track_mark_unformatted(d, i);

    d->tags = memalloc(sizeof(*d->tags));
    d->tags->tag.id = DSKTAG_end;
}

void dsk_init(struct disk *d)
{
    _dsk_init(d, 168);
}

static struct container *dsk_open(struct disk *d)
{
    struct disk_header dh;
    struct track_header th;
    struct tag_header tagh;
    struct disk_list_tag *dltag, **pprevtag;
    struct disktag *dtag;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, bytes_per_th, read_bytes_per_th;
    off_t off;

    read_exact(d->fd, &dh, sizeof(dh));
    if (strncmp(dh.signature, "DSK\0", 4) ||
        (be16toh(dh.version) != 0))
        return NULL;

    di = memalloc(sizeof(*di));
    di->nr_tracks = be16toh(dh.nr_tracks);
    di->flags = be16toh(dh.flags);
    di->track = memalloc(di->nr_tracks * sizeof(*ti));
    read_bytes_per_th = bytes_per_th = be16toh(dh.bytes_per_thdr);
    if (read_bytes_per_th > sizeof(*ti))
        read_bytes_per_th = sizeof(*ti);

    for (i = 0; i < di->nr_tracks; i++) {
        memset(&th, 0, sizeof(th));
        read_exact(d->fd, &th, read_bytes_per_th);
        ti = &di->track[i];
        init_track_info(ti, be16toh(th.type));
        ti->flags = be16toh(th.flags);
        ti->nr_sectors = be16toh(th.nr_sectors);
        ti->bytes_per_sector = be16toh(th.bytes_per_sector);
        memcpy(ti->valid_sectors, th.valid_sectors, sizeof(th.valid_sectors));
        ti->len = be32toh(th.len);
        ti->data_bitoff = be32toh(th.data_bitoff);
        ti->total_bits = be32toh(th.total_bits);
        off = lseek(d->fd, bytes_per_th-read_bytes_per_th, SEEK_CUR);
        lseek(d->fd, be32toh(th.off), SEEK_SET);
        ti->dat = memalloc(ti->len);
        read_exact(d->fd, ti->dat, ti->len);
        lseek(d->fd, off, SEEK_SET);
    }

    pprevtag = &d->tags;
    do {
        read_exact(d->fd, &tagh, sizeof(tagh));
        dltag = memalloc(sizeof(*dltag) + be16toh(tagh.len));
        dtag = &dltag->tag;
        dtag->id = be16toh(tagh.id);
        dtag->len = be16toh(tagh.len);
        read_exact(d->fd, dtag+1, dtag->len);
        tag_swizzle(dtag);
        *pprevtag = dltag;
        pprevtag = &dltag->next;
    } while (dtag->id != DSKTAG_end);
    *pprevtag = NULL;

    d->di = di;
    return &container_dsk;
}

static void dsk_close(struct disk *d)
{
    struct disk_header dh;
    struct track_header th;
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct disk_list_tag *dltag;
    struct disktag *dtag;
    unsigned int i, datoff;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    memcpy(dh.signature, "DSK\0", 4);
    dh.version = 0;
    dh.nr_tracks = htobe16(di->nr_tracks);
    dh.bytes_per_thdr = htobe16(sizeof(th));
    dh.flags = htobe16(di->flags);
    write_exact(d->fd, &dh, sizeof(dh));

    datoff = sizeof(dh) + di->nr_tracks * sizeof(th);
    for (dltag = d->tags; dltag != NULL; dltag = dltag->next)
        datoff += sizeof(struct tag_header) + dltag->tag.len;

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        th.type = htobe16(ti->type);
        th.flags = htobe16(ti->flags);
        th.nr_sectors = htobe16(ti->nr_sectors);
        th.bytes_per_sector = htobe16(ti->bytes_per_sector);
        memcpy(th.valid_sectors, ti->valid_sectors, sizeof(th.valid_sectors));
        th.off = htobe32(datoff);
        th.len = htobe32(ti->len);
        th.data_bitoff = htobe32(ti->data_bitoff);
        th.total_bits = htobe32(ti->total_bits);
        write_exact(d->fd, &th, sizeof(th));
        datoff += ti->len;
    }

    for (dltag = d->tags; dltag != NULL; dltag = dltag->next) {
        struct tag_header tagh;
        dtag = &dltag->tag;
        tagh.id = htobe16(dtag->id);
        tagh.len = htobe16(dtag->len);
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

int dsk_write_raw(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];
    unsigned int ns_per_cell = 0, default_len;

    memset(ti, 0, sizeof(*ti));
    init_track_info(ti, type);

    switch (handlers[type]->density) {
    case trkden_single: ns_per_cell = 4000u; break;
    case trkden_double: ns_per_cell = 2000u; break;
    case trkden_high: ns_per_cell = 1000u; break;
    case trkden_extra: ns_per_cell = 500u; break;
    default: BUG();
    }
    stream_set_density(s, ns_per_cell);
    default_len = (DEFAULT_BITS_PER_TRACK(d) * 2000u) / ns_per_cell;
    ti->total_bits = default_len;

    if (stream_select_track(s, tracknr) == 0)
        ti->dat = handlers[type]->write_raw(d, tracknr, s);

    if (ti->dat == NULL) {
        track_mark_unformatted(d, tracknr);
        ti->typename = "Unformatted*";
        return -1;
    }

    stream_reset(s);
    stream_next_index(s);

    if (ti->total_bits == 0) {
        ti->total_bits = s->track_len_bc ? : default_len;
    } else if (ti->total_bits == TRK_WEAK) {
        /* nothing */
    } else if (((s->track_len_bc - (s->track_len_bc/50)) > ti->total_bits) ||
               ((s->track_len_bc + (s->track_len_bc/50)) < ti->total_bits)) {
        fprintf(stderr, "*** T%u.%u: Unexpected track length (seen %u, "
                "expected %u)\n", cyl(tracknr), hd(tracknr),
                s->track_len_bc, ti->total_bits);
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
