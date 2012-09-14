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
enum bitcell_encoding {
    bc_raw,           /* emit all bits; do not insert clock bits */
    bc_mfm,           /* emit all data bits, in order */
    bc_mfm_even,      /* emit even-numbered data bits only */
    bc_mfm_odd,       /* emit odd-numbered data bits only */
    bc_mfm_even_odd,  /* emit all even-numbered bits; then odd-numbered */
    bc_mfm_odd_even   /* emit all odd-numbered bits; then even-numbered */
};

/* Track buffer: this is opaque to encoders, updated via tbuf_* helpers. */
struct tbuf {
    struct track_raw raw;
    struct disk *disk;
    uint32_t prng_seed;
    uint32_t start, pos;
    uint8_t prev_data_bit;
    uint16_t crc16_ccitt;
    bool_t disable_auto_sector_split;
    void (*bit)(struct tbuf *, uint16_t speed,
                enum bitcell_encoding enc, uint8_t dat);
    void (*gap)(struct tbuf *, uint16_t speed, unsigned int bits);
    void (*weak)(struct tbuf *, uint16_t speed, unsigned int bits);
};

/* Append new raw track data into a track buffer. */
void tbuf_init(struct tbuf *, uint32_t bitstart, uint32_t bitlen);
void tbuf_bits(struct tbuf *, uint16_t speed,
               enum bitcell_encoding enc, unsigned int bits, uint32_t x);
void tbuf_bytes(struct tbuf *, uint16_t speed,
                enum bitcell_encoding enc, unsigned int bytes, void *data);
void tbuf_gap(struct tbuf *, uint16_t speed, unsigned int bits);
void tbuf_weak(struct tbuf *, uint16_t speed, unsigned int bits);
void tbuf_start_crc(struct tbuf *tbuf);
void tbuf_emit_crc16_ccitt(struct tbuf *tbuf, uint16_t speed);
void tbuf_disable_auto_sector_split(struct tbuf *tbuf);

#define TBUF_PRNG_INIT 0xae659201u
uint16_t tbuf_rnd16(struct tbuf *tbuf);

enum track_density {
    trkden_double, /* default */
    trkden_high,
    trkden_single,
    trkden_extra
};

/* Track handler -- interface for various raw-bitcell analysers/encoders. */
struct track_handler {
    enum track_density density;
    unsigned int bytes_per_sector;
    unsigned int nr_sectors;
    void *(*write_raw)(
        struct disk *, unsigned int tracknr, struct stream *);
    void (*read_raw)(
        struct disk *, unsigned int tracknr, struct tbuf *);
    void *(*write_sectors)(
        struct disk *, unsigned int tracknr, struct track_sectors *);
    void (*read_sectors)(
        struct disk *, unsigned int tracknr, struct track_sectors *);
};

/* Array of supported raw-bitcell analysers/handlers. */
extern const struct track_handler *handlers[];

/* Set up a track with defaults for a given track format. */
void init_track_info(struct track_info *ti, enum track_type type);

/* Container -- interface for a disk-image container format. */
struct container {
    /* Create a brand new empty container. */
    void (*init)(struct disk *);
    /* Open an existing container file. */
    struct container *(*open)(struct disk *);
    /* Close, writing back any pending changes. */
    void (*close)(struct disk *);
    /* Analyse and write a raw stream to given track in container. */
    int (*write_raw)(struct disk *, unsigned int tracknr,
                     enum track_type, struct stream *);
};

/* Supported container formats. */
extern struct container container_adf;
extern struct container container_eadf;
extern struct container container_dsk;
extern struct container container_img;
extern struct container container_ipf;

/* Helpers for container implementations: defaults for init() & write_raw(). */
void dsk_init(struct disk *d);
int dsk_write_raw(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s);

/* Decode helpers for MFM analysers. */
uint32_t mfm_decode_bits(enum bitcell_encoding enc, uint32_t x);
void mfm_decode_bytes(
    enum bitcell_encoding enc, unsigned int bytes, void *in, void *out);
uint32_t mfm_encode_word(uint32_t w);
uint32_t amigados_checksum(void *dat, unsigned int bytes);

/* IBM format decode helpers. */
struct ibm_idam { uint8_t cyl, head, sec, no; };
int ibm_scan_mark(struct stream *s, uint16_t mark, unsigned int max_scan);
int ibm_scan_idam(struct stream *s, struct ibm_idam *idam);
int ibm_scan_dam(struct stream *s);

#define trk_warn(ti,trk,msg,a...) \
    printf("*** T%u: %s: " msg "\n", trk, (ti)->typename, ## a)

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
