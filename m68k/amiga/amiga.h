/******************************************************************************
 * amiga.h
 * 
 * Glue for Amiga emulation.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __AMIGA_H__
#define __AMIGA_H__

#include <stdint.h>
#include <stdio.h>

#include <libdisk/disk.h>
#include <libdisk/util.h>

#include <m68k/m68k_emulate.h>
#include <amiga/cia.h>
#include <amiga/disk.h>
#include <amiga/event.h>
#include <amiga/logging.h>
#include <amiga/mem.h>

/* PAL Amiga CPU runs at 1.709379 MHz */
#define M68K_CYCLE_NS 141

#define ROM_BASE 0xff0000
#define ROM_SIZE (256*1024)

struct amiga_state {
    /* 68000 register state */
    struct m68k_emulate_ctxt ctxt;

    /* Temp buffer for m68k addr_name() callback */
    char addr_name[16];

    /* Emulated RAM/ROM */
    struct memory *memory;
    struct memory *ram, *rom;

    /* Emulated CIA chips */
    struct cia ciaa, ciab;

    /* Disks */
    struct amiga_disk disk;

    /* Passage of time. */
    struct event_base event_base;

    /* Logging. */
    enum loglevel max_loglevel;
    FILE *logfile;

    /* Custom registers. */
    uint16_t custom[256];
};

#define offsetof(a,b) __builtin_offsetof(a,b)
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

void __assert_failed(
    struct amiga_state *s, const char *file, unsigned int line);
#define ASSERT(p) do {                                          \
        if ( !(p) ) __assert_failed(s, __FILE__, __LINE__);     \
} while (0)

void amiga_init(struct amiga_state *, unsigned int mem_size);
int amiga_emulate(struct amiga_state *);

void amiga_insert_df0(const char *filename);

void exec_init(struct amiga_state *);

#endif /* __AMIGA_H__ */
