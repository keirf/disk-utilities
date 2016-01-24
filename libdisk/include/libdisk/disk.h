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

struct disktag {
    uint16_t id;
    uint16_t len;
};

#define DSKTAG_rnc_pdos_key 1
struct disktag_rnc_pdos_key {
    struct disktag tag;
    uint32_t key;
};

#define DSKTAG_disk_nr 2
struct disktag_disk_nr {
    struct disktag tag;
    uint32_t disk_nr;
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

#define DISKFL_read_only     (1u<<0)
#define DISKFL_kryoflux_hack (1u<<1)
#define DISKFL_rpm_shift     2
#define DISKFL_rpm(rpm)      ((rpm)<<DISKFL_rpm_shift)

struct disk *disk_create(const char *name, unsigned int flags);
struct disk *disk_open(const char *name, unsigned int flags);
void disk_close(struct disk *);

const char *disk_get_format_id_name(enum track_type type);
const char *disk_get_format_desc_name(enum track_type type);

void track_get_format_name(
    struct disk *d, unsigned int tracknr, char *str, size_t size);

/* Valid until the disk is closed (disk_close()). */
struct disk_info *disk_get_info(struct disk *);

struct disktag *disk_get_tag_by_id(struct disk *d, uint16_t id);
struct disktag *disk_get_tag_by_idx(struct disk *d, unsigned int idx);
struct disktag *disk_set_tag(
    struct disk *d, uint16_t id, uint16_t len, void *dat);

/* Average bitcell timing: <time-per-revolution>/<#-bitcells>. Non-uniform
 * track timings are represented by fractional multiples of this average. */
#define SPEED_AVG 1000u

/* Weak bits. Regions of weak bits are timed at SPEED_AVG. */
#define SPEED_WEAK 0xfffeu

struct track_raw {
    /* Index-aligned bitcells. bitcell[i] = bits[i/8] >> -(i-7). */
    uint8_t *bits;
    /* Index-aligned per-bitcell speed, relative to SPEED_AVG. */
    uint16_t *speed;
    /* Number of bitcells in this track. */
    uint32_t bitlen;
    /* First and list bitcells written by the format handler. */
    uint32_t data_start_bc, data_end_bc;
    /* Bitcell offset of the write splice. */
    uint32_t write_splice_bc;
    /* Any weak/random bits in this track? */
    uint8_t has_weak_bits;
};
struct track_raw *track_alloc_raw_buffer(struct disk *d);
void track_free_raw_buffer(struct track_raw *);
void track_purge_raw_buffer(struct track_raw *);
void track_read_raw(struct track_raw *, unsigned int tracknr);
int track_write_raw(
    struct track_raw *, unsigned int tracknr, enum track_type,
    unsigned int rpm);
int track_write_raw_from_stream(
    struct disk *, unsigned int tracknr, enum track_type, struct stream *s);

struct track_sectors {
    uint8_t *data;
    uint32_t nr_bytes;
};
struct track_sectors *track_alloc_sector_buffer(struct disk *d);
void track_free_sector_buffer(struct track_sectors *);
void track_purge_sector_buffer(struct track_sectors *);
int track_read_sectors(struct track_sectors *, unsigned int tracknr);
int track_write_sectors(
    struct track_sectors *, unsigned int tracknr, enum track_type);

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
