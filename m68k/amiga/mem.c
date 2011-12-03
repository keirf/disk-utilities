/*
 * amiga/mem.c
 * 
 * Emulate RAM/ROM accesses.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <amiga/amiga.h>

#define SUBSYSTEM subsystem_mem

static struct memory *find_memory(
    struct amiga_state *s, uint32_t addr, uint32_t bytes)
{
    struct memory *m = s->memory;
    while (m && (m->end < (addr + bytes - 1)))
        m = m->next;
    return (m && (m->start <= addr)) ? m : NULL;
}

int mem_read(uint32_t addr, uint32_t *val, unsigned int bytes,
             struct amiga_state *s)
{
    struct memory *m = find_memory(s, addr, bytes);

    if (m == NULL) {
        log_warn("Read %u bytes non-RAM", bytes);
        return M68KEMUL_UNHANDLEABLE;
        *val = bytes == 1 ? 0xff : bytes == 2 ? 0xffff : 0xffffffff;
        return M68KEMUL_OKAY;
    }

    addr -= m->start;
    switch (bytes) {
    case 1:
        *val = *(uint8_t *)&m->dat[addr];
        break;
    case 2:
        *val = ntohs(*(uint16_t *)&m->dat[addr]);
        break;
    case 4:
        *val = ntohl(*(uint32_t *)&m->dat[addr]);
        break;
    default:
        return M68KEMUL_UNHANDLEABLE;
    }

    return M68KEMUL_OKAY;
}

int mem_write(uint32_t addr, uint32_t val, unsigned int bytes,
              struct amiga_state *s)
{
    struct memory *m = find_memory(s, addr, bytes);

    if (m == NULL) {
        log_warn("Write %u bytes non-RAM", bytes);
        return M68KEMUL_UNHANDLEABLE;
        return M68KEMUL_OKAY;
    }

    addr -= m->start;
    switch (bytes) {
    case 1:
        *(uint8_t *)&m->dat[addr] = val;
        break;
    case 2:
        *(uint16_t *)&m->dat[addr] = htons(val);
        break;
    case 4:
        *(uint32_t *)&m->dat[addr] = htonl(val);
        break;
    default:
        return M68KEMUL_UNHANDLEABLE;
    }

    return M68KEMUL_OKAY;
}

static void regions_dump(struct region *r)
{
    printf("Region list: ");
    while (r) {
        printf("%x-%x, ", r->start, r->end);
        r = r->next;
    }
    printf("\n");
}

void mem_reserve(struct amiga_state *s, uint32_t start, uint32_t bytes)
{
    struct memory *m = find_memory(s, start, bytes);
    struct region *r, *n, **pprev;
    uint32_t end = start + bytes - 1;

    ASSERT(m != NULL);

    regions_dump(m->free);

    pprev = &m->free;
    while (((r = *pprev) != NULL) && (r->end < start))
        pprev = &r->next;

    ASSERT((r != NULL) && (r->start <= start) && (r->end >= end));
    if (r->start == start) {
        r->start = end + 1;
    } else if (r->end == end) {
        r->end = start - 1;
    } else {
        n = memalloc(sizeof(*n));
        n->start = end + 1;
        n->end = r->end;
        n->next = r->next;
        r->end = start - 1;
        r->next = n;
    }

    if (r->start > r->end) {
        ASSERT(r->start == (r->end + 1));
        *pprev = r->next;
        memfree(r);
    }

    regions_dump(m->free);
}

uint32_t mem_alloc(struct amiga_state *s, struct memory *m, uint32_t bytes)
{
    uint32_t addr;
    struct region *r, **pprev;

    regions_dump(m->free);

    pprev = &m->free;
    while (((r = *pprev) != NULL) && ((r->end - r->start + 1) < bytes))
        pprev = &r->next;

    if (r == NULL)
        return 0;

    addr = r->start;
    r->start += bytes;
    if (r->start > r->end) {
        ASSERT(r->start == (r->end + 1));
        *pprev = r->next;
        memfree(r);
    }

    regions_dump(m->free);

    return addr;
}

void mem_free(struct amiga_state *s, uint32_t addr, uint32_t bytes)
{
    struct memory *m = find_memory(s, addr, bytes);
    struct region *r, *prev, **pprev;

    ASSERT(m != NULL);

    regions_dump(m->free);

    pprev = &m->free;
    while (((r = *pprev) != NULL) && (r->end < addr))
        pprev = &r->next;

    if ((r != NULL) && (r->start == (addr + bytes))) {
        r->start -= bytes;
    } else {
        r = memalloc(sizeof(*r));
        r->start = addr;
        r->end = addr + bytes - 1;
        r->next = *pprev;
        *pprev = r;
    }

    prev = container_of(pprev, struct region, next);
    if ((pprev != &m->free) && (prev->end >= (addr-1))) {
        ASSERT(prev->end == (addr-1));
        prev->end = r->end;
        prev->next = r->next;
        memfree(r);
    }

    memset(&m->dat[addr - m->start], 0xaa, bytes);

    regions_dump(m->free);
}

struct memory *mem_init(struct amiga_state *s, uint32_t start, uint32_t bytes)
{
    struct memory *m, *curr, **pprev;

    m = memalloc(sizeof(*m) + bytes);

    m->start = start;
    m->end = start + bytes - 1;
    m->dat = (uint8_t *)(m + 1);

    m->free = memalloc(sizeof(struct region));
    m->free->next = NULL;
    m->free->start = m->start;
    m->free->end = m->end;

    pprev = &s->memory;
    while (((curr = *pprev) != NULL) && (curr->start < m->start))
        pprev = &curr->next;
    m->next = curr;
    *pprev = m;

    return m;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
