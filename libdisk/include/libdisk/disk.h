/******************************************************************************
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
    TRKTYP_unformatted,
    TRKTYP_amigados,
    TRKTYP_amigados_labelled,
    TRKTYP_copylock,
    TRKTYP_lemmings,
    TRKTYP_rnc_pdos
};

struct track_info {
    /* Enumeration */
    uint16_t type;
    const char *typename;

    uint16_t flags;

    /* Sector layout and vailidity. */
    uint16_t bytes_per_sector;
    uint8_t  nr_sectors;
    uint32_t valid_sectors; /* bitmap of valid sectors */

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

struct disk_info {
    uint16_t nr_tracks;
    uint16_t flags;
    struct track_info *track;
};

struct disk;
struct stream;

#pragma GCC visibility push(default)

struct disk *disk_create(const char *name);
struct disk *disk_open(const char *name, int read_only, int quiet);
void disk_close(struct disk *);

/* Valid until the disk is closed (disk_close()). */
struct disk_info *disk_get_info(struct disk *);

void track_read_mfm(struct disk *d, unsigned int tracknr,
                    uint8_t **mfm, uint16_t **speed, uint32_t *bitlen);
void track_write_mfm_from_stream(
    struct disk *, unsigned int tracknr, struct stream *s);
void track_write_mfm(
    struct disk *, unsigned int tracknr,
    uint8_t *mfm, uint16_t *speed, uint32_t bitlen);

#pragma GCC visibility pop

#endif /* __LIBDISK_DISK_H__ */
