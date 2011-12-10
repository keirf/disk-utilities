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

struct disk_list_tag {
    struct disk_list_tag *next;
    struct disk_tag tag;
};

struct container;

struct disk {
    int fd;
    bool_t read_only;
    struct container *container;
    struct disk_info *di;
    struct disk_list_tag *tags;
};

struct track_buffer {
    uint8_t *mfm;
    uint16_t *speed;
    uint32_t start, pos, len;
    uint8_t prev_data_bit;
};

enum tbuf_data_type {
    TB_raw,        /* emit all bits; do not insert clock bits */
    TB_all,        /* emit all data bits */
    TB_even,       /* emit even-numbered data bits only */
    TB_odd,        /* emit odd-numbered data bits only */
    TB_even_odd,   /* emit all even-numbered bits; then odd-numbered */
    TB_odd_even    /* emit all odd-numbered bits; then even-numbered */
};

void tbuf_init(struct track_buffer *);
void tbuf_bits(struct track_buffer *, uint16_t speed,
               enum tbuf_data_type type, unsigned int bits, uint32_t x);
void tbuf_bytes(struct track_buffer *, uint16_t speed,
                enum tbuf_data_type type, unsigned int bytes, void *data);

struct track_handler {
    unsigned int bytes_per_sector;
    unsigned int nr_sectors;
    void *(*write_mfm)(
        struct disk *, unsigned int tracknr, struct stream *);
    void (*read_mfm)(
        struct disk *, unsigned int tracknr, struct track_buffer *);
};

extern const struct track_handler *handlers[];

void init_track_info(struct track_info *ti, enum track_type type);

struct container {
    void (*init)(struct disk *);
    int (*open)(struct disk *);
    void (*close)(struct disk *);
    int (*write_mfm)(struct disk *, unsigned int tracknr,
                     enum track_type, struct stream *);
};

extern struct container container_adf;
extern struct container container_dsk;

extern uint16_t copylock_decode_word(uint32_t);
extern uint32_t mfm_decode_amigados(void *dat, unsigned int longs);

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
