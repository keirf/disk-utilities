#ifndef __DISK_PRIVATE_H__
#define __DISK_PRIVATE_H__

#include <libdisk/disk.h>
#include <libdisk/stream.h>

#define DEFAULT_SPEED 1000u

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
    void *(*write_mfm)(
        unsigned int tracknr, struct track_header *, struct stream *);
    void (*read_mfm)(
        unsigned int tracknr, struct track_buffer *tbuf,
        struct track_header *th, void *data);
};

void write_valid_sector_map(struct track_header *, uint32_t);

#endif /* __DISK_PRIVATE_H__ */
