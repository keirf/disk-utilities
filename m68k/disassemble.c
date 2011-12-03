/******************************************************************************
 * m68k/disassemble.c
 * 
 * Disassemble 680x0 code.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <utime.h>

#include <amiga/amiga.h>
#include <amiga/custom.h>

#define offsetof(a,b) __builtin_offsetof(a,b)
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define MEM_SIZE (512*1024) /* our system has 512kB RAM */

struct m68k_state {
    struct m68k_emulate_ctxt ctxt;
    char addr_name[16];
    char *mem;
};

static int emul_read(uint32_t addr, uint32_t *val, unsigned int bytes,
                     struct m68k_emulate_ctxt *ctxt)
{
    struct m68k_state *s = container_of(ctxt, struct m68k_state, ctxt);

    if (((addr >= 0xdff000) && (addr <= 0xdff200)) ||
        ((addr >= 0xbfd000) && (addr <= 0xbfef01))) {
        *val = 0xdeadbeef;
        return M68KEMUL_OKAY;
    }

    if ((addr > MEM_SIZE) || ((addr+bytes) > MEM_SIZE))
        return M68KEMUL_UNHANDLEABLE;

    switch (bytes) {
    case 1:
        *val = *(uint8_t *)&s->mem[addr];
        break;
    case 2:
        *val = ntohs(*(uint16_t *)&s->mem[addr]);
        break;
    case 4:
        *val = ntohl(*(uint32_t *)&s->mem[addr]);
        break;
    default:
        return M68KEMUL_UNHANDLEABLE;
    }

    return M68KEMUL_OKAY;
}

static int emul_write(uint32_t addr, uint32_t val, unsigned int bytes,
                      struct m68k_emulate_ctxt *ctxt)
{
    struct m68k_state *s = container_of(ctxt, struct m68k_state, ctxt);

    if (((addr >= 0xdff000) && (addr <= 0xdff200)) ||
        ((addr >= 0xbfd000) && (addr <= 0xbfef01)))
        return M68KEMUL_OKAY;

    if ((addr > MEM_SIZE) || ((addr+bytes) > MEM_SIZE))
        return M68KEMUL_UNHANDLEABLE;

    switch (bytes) {
    case 1:
        *(uint8_t *)&s->mem[addr] = val;
        break;
    case 2:
        *(uint16_t *)&s->mem[addr] = htons(val);
        break;
    case 4:
        *(uint32_t *)&s->mem[addr] = htonl(val);
        break;
    default:
        return M68KEMUL_UNHANDLEABLE;
    }

    return M68KEMUL_OKAY;
}

static const char *emul_addr_name(
    uint32_t addr, struct m68k_emulate_ctxt *ctxt)
{
    struct m68k_state *s = container_of(ctxt, struct m68k_state, ctxt);
    char ciax;

    if (addr > 0xdff000) { /* skip dff000 itself */
        if (addr & 1)
            return NULL;
        addr = (addr - 0xdff000) >> 1;
        return (addr < ARRAY_SIZE(custom_reg_name)
                ? custom_reg_name[addr] : NULL);
    }

    if (addr >= 0xbfe001) {
        addr -= 0xbfe001;
        ciax = 'a';
    cia:
        if (addr & 0xff)
            return NULL;
        addr >>= 8;
        if (addr >= ARRAY_SIZE(cia_reg_name))
            return NULL;
        sprintf(s->addr_name, "cia%c%s", ciax, cia_reg_name[addr]);
        return s->addr_name;
    }

    if (addr >= 0xbfd000) {
        addr -= 0xbfd000;
        ciax = 'b';
        goto cia;
    }

    return NULL;
}

static struct m68k_emulate_ops emul_ops = {
    .read = emul_read,
    .write = emul_write,
    .addr_name = emul_addr_name
};

/* read_exact */
#include "../libdisk/util.c"

int main(int argc, char **argv)
{
    struct m68k_state s = { { 0 } };
    struct m68k_regs regs = { { 0 } };
    char *p;
    int i, j, fd, zeroes_run = 0;
    uint32_t off, len, base;

    if (argc != 5)
        errx(1, "Usage: %s <infile> <off> <len> <base>",
             argv[0]);

    fd = open(argv[1], O_RDONLY);
    if (fd == -1)
        err(1, "%s", argv[1]);

    off = strtol(argv[2], NULL, 16);
    len = strtol(argv[3], NULL, 16);
    base = strtol(argv[4], NULL, 16);

    if (len == 0) {
        off_t sz = lseek(fd, 0, SEEK_END);
        len = sz - off;
    }

    if ((base+len) > MEM_SIZE)
        errx(1, "Image cannot be loaded into %ukB RAM\n", MEM_SIZE>>10);

    s.mem = memalloc(MEM_SIZE);

    if (lseek(fd, off, SEEK_SET) != off)
        err(1, NULL);
    read_exact(fd, &s.mem[base], len);
    close(fd);

    for (i = 0; i < argc; i++)
        printf("%s ", argv[i]);
    printf("\n");

    regs.pc = base;
    regs.a[7] = 0x2000;
    regs.xsp = MEM_SIZE;

    s.ctxt.regs = &regs;
    s.ctxt.ops = &emul_ops;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 1;

    while (regs.pc < (base + len)) {
        uint32_t pc = regs.pc;

        (void)m68k_emulate(&s.ctxt);

        /* Skip runs of ori.b #0,d0 */
        if ((s.ctxt.op_words == 2) &&
            (s.ctxt.op[0] == 0) && (s.ctxt.op[1] == 0)) {
            if (++zeroes_run > 2)
                goto skip;
        } else {
            if (zeroes_run >= 2) {
                printf("      [%u more]\n", zeroes_run-1);
                printf("-------------------------------\n");
            }
            zeroes_run = 0;
        }

        printf("%08x  ", pc);

        if (zeroes_run == 2) {
            printf(".... .... ");
            goto skip;
        }

        for (j = 0; j < 3; j++) {
            if (j < s.ctxt.op_words)
                printf("%04x ", s.ctxt.op[j]);
            else
                printf("     ");
        }
        if ((p = strchr(s.ctxt.dis, '\t')) != NULL)
            *p = '\0';
        printf(" %s", s.ctxt.dis);
        if (p) {
            int spaces = 8-(p-s.ctxt.dis);
            if (spaces < 1)
                spaces = 1;
            printf("%*s%s", spaces, "", p+1);
        }
        printf("\n");
        if (j < s.ctxt.op_words) {
            printf("%08x  ", pc + 2*j);
            while (j < s.ctxt.op_words)
                printf("%04x ", s.ctxt.op[j++]);
            printf("\n");
        }

    skip:
        regs.pc = pc + s.ctxt.op_words*2;
    }

    return 0;
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
