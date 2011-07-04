#ifndef __DISK_PRIVATE_H__
#define __DISK_PRIVATE_H__

#include <libdisk/disk.h>
#include <libdisk/stream.h>

#define DEFAULT_SPEED 1000u

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
    enum track_type prev_type;
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
    TBUFDAT_raw,
    TBUFDAT_all,
    TBUFDAT_even,
    TBUFDAT_odd
};

void tbuf_init(struct track_buffer *);
void tbuf_finalise(struct track_buffer *);
void tbuf_bits(struct track_buffer *, uint16_t speed,
               enum tbuf_data_type type, unsigned int bits, uint32_t x);
void tbuf_bytes(struct track_buffer *, uint16_t speed,
                enum tbuf_data_type type, unsigned int bytes, void *data);

struct track_handler {
    const char *name;
    enum track_type type;
    unsigned int bytes_per_sector;
    unsigned int nr_sectors;
    void *(*write_mfm)(
        struct disk *, unsigned int tracknr, struct stream *);
    void (*read_mfm)(
        struct disk *, unsigned int tracknr, struct track_buffer *);
};

extern const struct track_handler *handlers[];

void init_track_info_from_handler_info(
    struct track_info *ti, const struct track_handler *thnd);

struct container {
    void (*init)(struct disk *);
    int (*open)(struct disk *, bool_t quiet);
    void (*close)(struct disk *);
    void (*write_mfm)(struct disk *, unsigned int tracknr, struct stream *);
};

extern struct container container_adf;
extern struct container container_dsk;

extern uint16_t copylock_decode_word(uint32_t);
extern uint32_t mfm_decode_amigados(void *dat, unsigned int longs);

#endif /* __DISK_PRIVATE_H__ */
