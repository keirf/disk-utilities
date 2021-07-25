#ifndef __PRIVATE_DISK_H__
#define __PRIVATE_DISK_H__

#include <libdisk/disk.h>
#include <libdisk/stream.h>
#include <private/util.h>

#define DEFAULT_RPM 300

/* Determined empirically -- larger than expected for 2us bitcell @ 300rpm */
#define DEFAULT_BITS_PER_TRACK(d) (100150*300/(d)->rpm)

/* Track handlers can tag a disk with format metadata (e.g., encrypt keys). */
struct disk_list_tag {
    struct disk_list_tag *next;
    struct disktag tag;
};

struct container;

/* Private data relating to an open disk. */
struct disk {
    int fd;
    bool_t read_only;
    bool_t kryoflux_hack;
    unsigned int rpm;
    struct container *container;
    struct disk_info *di;
    struct disk_list_tag *tags;
};

/* How to interpret data being appended to a track buffer. */
enum bitcell_encoding {
    bc_raw,           /* emit all bits; do not insert clock bits */
    bc_mfm,           /* emit all data bits, in order, MFM clock bits */
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
    uint8_t gap_fill_byte;
    uint16_t crc16_ccitt;
    bool_t disable_auto_sector_split;
    void (*bit)(struct tbuf *, uint16_t speed,
                enum bitcell_encoding enc, uint8_t dat);
    void (*gap)(struct tbuf *, uint16_t speed, unsigned int bits);
    void (*weak)(struct tbuf *, unsigned int bits);
};

/* Append new raw track data into a track buffer. */
void tbuf_init(struct tbuf *, uint32_t bitstart, uint32_t bitlen);
void tbuf_bits(struct tbuf *, uint16_t speed,
               enum bitcell_encoding enc, unsigned int bits, uint32_t x);
void tbuf_bytes(struct tbuf *, uint16_t speed,
                enum bitcell_encoding enc, unsigned int bytes, void *data);
void tbuf_gap(struct tbuf *, uint16_t speed, unsigned int bits);
void tbuf_weak(struct tbuf *, unsigned int bits);
void tbuf_start_crc(struct tbuf *tbuf);
void tbuf_emit_crc16_ccitt(struct tbuf *tbuf, uint16_t speed);
void tbuf_disable_auto_sector_split(struct tbuf *tbuf);
void tbuf_gap_fill(struct tbuf *tbuf, uint16_t speed, uint8_t fill);
void tbuf_set_gap_fill_byte(struct tbuf *tbuf, uint8_t byte);

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
    void (*get_name)(
        struct disk *, unsigned int tracknr, char *, size_t);
    void *(*write_raw)(
        struct disk *, unsigned int tracknr, struct stream *);
    void (*read_raw)(
        struct disk *, unsigned int tracknr, struct tbuf *);
    void *(*write_sectors)(
        struct disk *, unsigned int tracknr, struct track_sectors *);
    void (*read_sectors)(
        struct disk *, unsigned int tracknr, struct track_sectors *);
    void *extra_data;
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
extern struct container container_hfe;
extern struct container container_imd;
extern struct container container_img;
extern struct container container_ipf;
extern struct container container_scp;
extern struct container container_jv3;

/* Helpers for container implementations: defaults for init() & write_raw(). */
void _dsk_init(struct disk *d, unsigned int nr_tracks);
void dsk_init(struct disk *d);
int dsk_write_raw(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s);

/* Decode/Encode helpers for MFM analysers. */
/* mfm_decode_word: Decode 32-bit MFM to 16-bit word. */
uint16_t mfm_decode_word(uint32_t w);
/* mfm_encode_word: Encode 17-bit word to 32-bit MFM (16 clock + 16 data). */
uint32_t mfm_encode_word(uint32_t w);
void mfm_decode_bytes(
    enum bitcell_encoding enc, unsigned int bytes, void *in, void *out);
void mfm_encode_bytes(
    enum bitcell_encoding enc, unsigned int bytes, void *in, void *out,
    uint8_t prev_bit);
uint32_t amigados_checksum(void *dat, unsigned int bytes);

/* IBM format decode helpers. */
struct ibm_idam { uint8_t cyl, head, sec, no, crc;};
#define IBM_MARK_IDAM 0xfe
#define IBM_MARK_DAM  0xfb
#define IBM_MARK_DDAM 0xf8
int ibm_scan_mark(struct stream *s, unsigned int max_scan, uint8_t *pmark);
int _ibm_scan_idam(struct stream *s, struct ibm_idam *idam);
int ibm_scan_idam(struct stream *s, struct ibm_idam *idam);
int ibm_scan_dam(struct stream *s);

void setup_ibm_mfm_track(
    struct disk *d, unsigned int tracknr,
    enum track_type type, unsigned int nr_secs, unsigned int no,
    uint8_t *sec_map, uint8_t *cyl_map, uint8_t *head_map,
    uint8_t *mark_map, uint8_t *dat);

void retrieve_ibm_mfm_track(
    struct disk *d, unsigned int tracknr,
    uint8_t **psec_map, uint8_t **pcyl_map,
    uint8_t **phead_map, uint8_t **pno_map,
    uint8_t **pmark_map, uint16_t **pcrc_map,
    uint8_t **pdat);

void setup_uniform_raw_track(
    struct disk *d, unsigned int tracknr,
    enum track_type type, unsigned int nr_bits,
    uint8_t *raw_dat);

bool_t track_is_copylock(struct track_info *ti);

#endif /* __PRIVATE_DISK_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
