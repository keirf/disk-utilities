/******************************************************************************
 * amiga.c
 * 
 * Glue for Amiga emulation.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <amiga/amiga.h>
#include <amiga/logging.h>
#include <amiga/custom.h>

#define SUBSYSTEM subsystem_main

#define CUSTOM_BASE 0xdff000
#define CIAA_BASE   0xbfe001
#define CIAB_BASE   0xbfd000

void *memalloc(size_t size)
{
    void *p = malloc(size);
    if ( p == NULL )
        err(1, NULL);
    memset(p, 0, size);
    return p;
}

void memfree(void *p)
{
    free(p);
}

void __assert_failed(
    struct amiga_state *s, const char *file, unsigned int line)
{
    errx(1, "Assertion failed at %s:%u", file, line);
}

static int amiga_read(uint32_t addr, uint32_t *val, unsigned int bytes,
                      struct m68k_emulate_ctxt *ctxt)
{
    struct amiga_state *s = container_of(ctxt, struct amiga_state, ctxt);

    if ( addr & 0xff000000 )
        log_warn("32-bit address access %08x @ PC=%08x", addr, ctxt->regs->pc);
    addr &= 0xffffff;

    if ( (addr & 0xfff0ff) == CIAB_BASE )
    {
        *val = cia_read_reg(s, &s->ciab, (addr >> 8) & 15);
        return M68KEMUL_OKAY;
    }

    if ( (addr & 0xfff0ff) == CIAA_BASE )
    {
        *val = cia_read_reg(s, &s->ciaa, (addr >> 8) & 15);
        return M68KEMUL_OKAY;
    }

    if ( (addr & 0xfff000) == CUSTOM_BASE )
    {
        addr -= CUSTOM_BASE;
        if ( bytes == 4 )
            *val = (custom_read_reg(s, addr) << 16)
                | custom_read_reg(s, addr + 2);
        else if ( bytes == 2 )
            *val = custom_read_reg(s, addr);
        else
        {
            *val = custom_read_reg(s, addr&~1);
            if ( !(addr & 1) ) *val >>= 8;
            *val = (uint8_t)*val;
        }
        return M68KEMUL_OKAY;
    }

    return mem_read(addr, val, bytes, s);
}

static int amiga_write(uint32_t addr, uint32_t val, unsigned int bytes,
                       struct m68k_emulate_ctxt *ctxt)
{
    struct amiga_state *s = container_of(ctxt, struct amiga_state, ctxt);

    if ( addr & 0xff000000 )
        log_warn("32-bit address access %08x @ PC=%08x", addr, ctxt->regs->pc);
    addr &= 0xffffff;

    if ( (addr & 0xfff0ff) == CIAB_BASE )
    {
        cia_write_reg(s, &s->ciab, (addr >> 8) & 15, val);
        return M68KEMUL_OKAY;
    }

    if ( (addr & 0xfff0ff) == CIAA_BASE )
    {
        cia_write_reg(s, &s->ciaa, (addr >> 8) & 15, val);
        return M68KEMUL_OKAY;
    }

    if ( (addr & 0xfff000) == CUSTOM_BASE )
    {
        addr -= CUSTOM_BASE;
        if ( bytes == 4 )
        {
            custom_write_reg(s, addr, val >> 16);
            custom_write_reg(s, addr+2, val);
        }
        else if ( bytes == 2 )
            custom_write_reg(s, addr, val);
        else
        {
            val = (uint8_t)val;
            custom_write_reg(s, addr&~1, val << (!(addr&1)?8:0));
        }
        return M68KEMUL_OKAY;
    }

    if ( (addr & 0xff0000) == 0xff0000 )
        return mem_write(addr, val, bytes, s);

    return mem_write(addr, val, bytes, s);
}

static const char *amiga_addr_name(
    uint32_t addr, struct m68k_emulate_ctxt *ctxt)
{
    struct amiga_state *s = container_of(ctxt, struct amiga_state, ctxt);
    char ciax;

    if ( addr > CUSTOM_BASE ) /* skip dff000 itself */
    {
        if ( addr & 1 )
            return NULL;
        addr = (addr - CUSTOM_BASE) >> 1;
        return (addr < ARRAY_SIZE(custom_reg_name)
                ? custom_reg_name[addr] : NULL);
    }

    if ( addr >= CIAA_BASE )
    {
        addr -= CIAA_BASE;
        ciax = 'a';
    cia:
        if ( addr & 0xff )
            return NULL;
        addr >>= 8;
        if ( addr >= ARRAY_SIZE(cia_reg_name) )
            return NULL;
        sprintf(s->addr_name, "cia%c%s", ciax, cia_reg_name[addr]);
        return s->addr_name;
    }

    if ( addr >= CIAB_BASE )
    {
        addr -= CIAB_BASE;
        ciax = 'b';
        goto cia;
    }

    return NULL;
}

static int amiga_deliver_exception(
    struct m68k_emulate_ctxt *ctxt, struct m68k_exception *exc)
{
    struct amiga_state *s = container_of(ctxt, struct amiga_state, ctxt);
    uint32_t target;

    if ( exc->vector != M68KVEC_trace )
    {
        (void)ctxt->ops->read(exc->vector*4, &target, 4, ctxt);
        log_warn("Exception %02x: %08x -> %08x",
                 exc->vector, ctxt->regs->pc, target);
    }

    return m68k_deliver_exception(ctxt, exc);
}

static struct m68k_emulate_ops amiga_m68k_ops = {
    .read = amiga_read,
    .write = amiga_write,
    .addr_name = amiga_addr_name,
    .deliver_exception = amiga_deliver_exception
};

int amiga_emulate(struct amiga_state *s)
{
    int rc = m68k_emulate(&s->ctxt);
    if ( (rc != M68KEMUL_OKAY) || !s->ctxt.emulate )
        return rc;
    s->event_base.current_time += s->ctxt.cycles * M68K_CYCLE_NS;
    fire_events(&s->event_base);
    return rc;
}

void amiga_init(struct amiga_state *s, unsigned int mem_size)
{
    memset(s, 0, sizeof(*s));
    s->ctxt.regs = memalloc(sizeof(*s->ctxt.regs));
    s->ctxt.ops = &amiga_m68k_ops;
    s->ram = mem_init(s, 0, mem_size);
    s->rom = mem_init(s, ROM_BASE, ROM_SIZE);
    exec_init(s);
    logging_init(s);
    disk_init(s);

    /* Reserve space for stacks. */
    mem_reserve(s, 0, 0x2000);
    s->ctxt.regs->a[7] = 0x2000; /* USP */
    s->ctxt.regs->xsp = 0x1000;  /* SSP */
}
