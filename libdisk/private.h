#ifndef __DISK_PRIVATE_H__
#define __DISK_PRIVATE_H__

#include <libdisk/disk.h>
#include <libdisk/stream.h>

/*
 * Average bitcell timing: <time-per-revolution>/<#-bitcells>. Non-uniform
 * track timings are represented by fractional multiples of this average.
 */
#define SPEED_AVG 1000u

/* Determined empirically -- larger than expected for 2us bitcell @ 300rpm */
#define DEFAULT_BITS_PER_TRACK       100150

/* Track handlers can tag a disk with format metadata (e.g., encrypt keys). */
struct disk_list_tag {
    struct disk_list_tag *next;
    struct disk_tag tag;
};

struct container;

/* Private data relating to an open disk. */
struct disk {
    int fd;
    bool_t read_only;
    struct container *container;
    struct disk_info *di;
    struct disk_list_tag *tags;
};

/* How to interpret data being appended to a track buffer. */
enum mfm_encoding {
    MFM_raw,       /* emit all bits; do not insert clock bits */
    MFM_all,       /* emit all data bits, in order */
    MFM_even,      /* emit even-numbered data bits only */
    MFM_odd,       /* emit odd-numbered data bits only */
    MFM_even_odd,  /* emit all even-numbered bits; then odd-numbered */
    MFM_odd_even   /* emit all odd-numbered bits; then even-numbered */
};

/* Track buffer: this is opaque to MFM encoders, updated via tbuf_* helpers. */
struct track_buffer {
    uint8_t *mfm;
    uint16_t *speed;
    uint32_t start, pos, len;
    uint8_t prev_data_bit;
    uint16_t crc16_ccitt;
    void (*bit)(struct track_buffer *, uint16_t speed,
                enum mfm_encoding enc, uint8_t dat);
    void (*gap)(struct track_buffer *, uint16_t speed, unsigned int bits);
};

/* Append new raw track data into a track buffer. */
void tbuf_init(struct track_buffer *, uint32_t bitstart, uint32_t bitlen);
void tbuf_bits(struct track_buffer *, uint16_t speed,
               enum mfm_encoding enc, unsigned int bits, uint32_t x);
void tbuf_bytes(struct track_buffer *, uint16_t speed,
                enum mfm_encoding enc, unsigned int bytes, void *data);
void tbuf_gap(struct track_buffer *, uint16_t speed, unsigned int bytes);
void tbuf_start_crc(struct track_buffer *tbuf);
void tbuf_emit_crc16_ccitt(struct track_buffer *tbuf, uint16_t speed);

/* MFM track handler -- interface for various MFM analysers/encoders. */
struct track_handler {
    unsigned int bytes_per_sector;
    unsigned int nr_sectors;
    void *(*write_mfm)(
        struct disk *, unsigned int tracknr, struct stream *);
    void (*read_mfm)(
        struct disk *, unsigned int tracknr, struct track_buffer *);
};

/* Array of supported MFM analysers/handlers. */
extern const struct track_handler *handlers[];

/* Set up a track with defaults for a given track format. */
void init_track_info(struct track_info *ti, enum track_type type);

/* Container -- interface for a disk-image container format. */
struct container {
    /* Create a brand new empty container. */
    void (*init)(struct disk *);
    /* Open an existing container file. */
    int (*open)(struct disk *);
    /* Close, writing back any pending changes. */
    void (*close)(struct disk *);
    /* Analyse and write an MFM stream to given track in container. */
    int (*write_mfm)(struct disk *, unsigned int tracknr,
                     enum track_type, struct stream *);
};

/* Supported container formats. */
extern struct container container_adf;
extern struct container container_dsk;
extern struct container container_ipf;

/* Helpers for container implementations: defaults for init() & write_mfm(). */
void dsk_init(struct disk *d);
int dsk_write_mfm(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s);

/* Decode helpers for MFM analysers. */
uint32_t mfm_decode_bits(enum mfm_encoding enc, uint32_t x);
void mfm_decode_bytes(
    enum mfm_encoding enc, unsigned int bytes, void *in, void *out);
uint32_t amigados_checksum(void *dat, unsigned int bytes);

#endif /* __DISK_PRIVATE_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
