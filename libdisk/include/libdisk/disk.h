/*
 * libdisk/disk.h
 * 
 * Custom disk layouts -- container format and handlers.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __LIBDISK_DISK_H__
#define __LIBDISK_DISK_H__

#include <stdint.h>

#define TRK_WEAK  (~0u)

enum track_type {
#define X(a,b) TRKTYP_##a,
#include <libdisk/track_types.h>
#undef X
};

struct track_info {
    /* Enumeration */
    uint16_t type;
    const char *typename;

    uint16_t flags;

    /* Sector layout and vailidity. */
    uint16_t bytes_per_sector;
    uint8_t  nr_sectors;
    uint8_t valid_sectors[8]; /* bitmap of valid sectors */

    /* Pointer and length of type-specific track data. */
    uint8_t *dat;
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

struct disk_tag {
    uint16_t id;
    uint16_t len;
};

#define DSKTAG_rnc_pdos_key 1
struct rnc_pdos_key {
    struct disk_tag tag;
    uint32_t key;
};

#define DSKTAG_end 0xffffu

struct disk_info {
    uint16_t nr_tracks;
    uint16_t flags;
    struct track_info *track;
};

struct disk;
struct stream;

#pragma GCC visibility push(default)

struct disk *disk_create(const char *name);
struct disk *disk_open(const char *name, int read_only);
void disk_close(struct disk *);

const char *disk_get_format_id_name(enum track_type type);
const char *disk_get_format_desc_name(enum track_type type);

/* Valid until the disk is closed (disk_close()). */
struct disk_info *disk_get_info(struct disk *);

struct disk_tag *disk_get_tag_by_id(struct disk *d, uint16_t id);
struct disk_tag *disk_get_tag_by_idx(struct disk *d, unsigned int idx);
struct disk_tag *disk_set_tag(
    struct disk *d, uint16_t id, uint16_t len, void *dat);

struct track_raw {
    uint8_t *bits;
    uint16_t *speed;
    uint32_t bitlen;
    uint8_t has_weak_bits;
};
struct track_raw *track_raw_alloc_buffer(struct disk *d);
void track_raw_free_buffer(struct track_raw *);
void track_raw_purge_buffer(struct track_raw *);
void track_raw_read(struct track_raw *, unsigned int tracknr);

int track_write_raw_from_stream(
    struct disk *, unsigned int tracknr, enum track_type type,
    struct stream *s);
int track_write_raw(
    struct disk *, unsigned int tracknr, enum track_type type,
    struct track_raw *);

void track_mark_unformatted(
    struct disk *, unsigned int tracknr);

int is_valid_sector(struct track_info *, unsigned int sector);
void set_sector_valid(struct track_info *, unsigned int sector);
void set_sector_invalid(struct track_info *, unsigned int sector);
void set_all_sectors_valid(struct track_info *ti);
void set_all_sectors_invalid(struct track_info *ti);

#pragma GCC visibility pop

#endif /* __LIBDISK_DISK_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
