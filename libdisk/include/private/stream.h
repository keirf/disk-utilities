#ifndef __PRIVATE_STREAM_H__
#define __PRIVATE_STREAM_H__

#include <libdisk/stream.h>
#include <private/util.h>

struct stream_type {
    struct stream *(*open)(const char *name, unsigned int data_rpm);
    void (*close)(struct stream *);
    int (*select_track)(struct stream *, unsigned int tracknr);
    void (*reset)(struct stream *);
    int (*next_flux)(struct stream *);
    const char *suffix[];
};

void stream_setup(
    struct stream *s, const struct stream_type *st,
    unsigned int drive_rpm, unsigned int data_rpm);

#endif /* __PRIVATE_STREAM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
