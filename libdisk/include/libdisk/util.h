/*
 * libdisk/util.h
 * 
 * Little helper utils.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __LIBDISK_UTIL_H__
#define __LIBDISK_UTIL_H__

#include <inttypes.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef offsetof
#define offsetof(a,b) __builtin_offsetof(a,b)
#endif
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef char bool_t;

#pragma GCC visibility push(default)

void *memalloc(size_t size);
void memfree(void *p);

void read_exact(int fd, void *buf, size_t count);
void write_exact(int fd, const void *buf, size_t count);

uint32_t crc32_add(const void *buf, size_t len, uint32_t crc);
uint32_t crc32(const void *buf, size_t len);

#pragma GCC visibility pop

#endif /* __LIBDISK_UTIL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
