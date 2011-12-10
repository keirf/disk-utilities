#ifndef __STREAM_PRIVATE_H__
#define __STREAM_PRIVATE_H__

#include <libdisk/stream.h>

struct stream_type {
    struct stream *(*open)(const char *name);
    void (*close)(struct stream *);
    void (*reset)(struct stream *, unsigned int tracknr);
    int (*next_bit)(struct stream *);
    const char *suffix[];
};

void index_reset(struct stream *s);

#endif /* __STREAM_PRIVATE_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
