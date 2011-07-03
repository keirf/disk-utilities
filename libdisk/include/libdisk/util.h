/******************************************************************************
 * libdisk/util.h
 * 
 * Little helper utils.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __LIBDISK_UTIL_H__
#define __LIBDISK_UTIL_H__

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define offsetof(a,b) __builtin_offsetof(a,b)
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

#pragma GCC visibility pop

#endif /* __LIBDISK_UTIL_H__ */
