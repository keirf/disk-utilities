/******************************************************************************
 * amiga/mem.h
 * 
 * Emulate RAM/ROM accesses.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __AMIGA_MEM_H__
#define __AMIGA_MEM_H__

struct region {
    struct region *next;
    uint32_t start, end;
};

struct watch {
    struct region region;
    int (*cb)(struct amiga_state *, uint32_t addr);
};

struct memory {
    struct memory *next;
    uint32_t start, end;
    uint8_t *dat;
    struct region *free;
    struct watch *watch;
};

void mem_reserve(struct amiga_state *s, uint32_t start, uint32_t bytes);
uint32_t mem_alloc(struct amiga_state *, struct memory *, uint32_t bytes);
void mem_free(struct amiga_state *, uint32_t addr, uint32_t bytes);

int mem_read(uint32_t addr, uint32_t *val, unsigned int bytes,
             struct amiga_state *);
int mem_write(uint32_t addr, uint32_t val, unsigned int bytes,
              struct amiga_state *);
struct memory *mem_init(struct amiga_state *, uint32_t start, uint32_t bytes);

#endif /* __AMIGA_MEM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
