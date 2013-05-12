/*
 * m68k/copylock.c
 * 
 * Copylock extracter.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>

#include <amiga/amiga.h>
#include <libdisk/util.h>

#define MEM_SIZE (512*1024) /* our system has 512kB RAM */

static void set_bit(unsigned int bit, char *map)
{
    map[bit/8] |= 1u << (bit&7);
}

static int test_bit(unsigned int bit, char *map)
{
    return !!(map[bit/8] & (1u << (bit&7)));
}

static void dump(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static int ctrl_c;
static void sigint_handler(int signum)
{
    ctrl_c = 1;
}

static void init_sigint_handler(void)
{
#if !defined(__MINGW32__)
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL))
        err(1, NULL);
#else
    /* No sigemptyset or sigaction in mingw. */
    signal(SIGINT, sigint_handler);
#endif
}

int main(int argc, char **argv)
{
    struct amiga_state s;
    struct m68k_regs *regs;
    char *p, *shadow, *bmap;
    int rc, i, fd, zeroes_run = 0;
    uint32_t off, len, base;

    if (argc != 6)
        errx(1, "Usage: %s <infile> <off> <len> <base> <df0_file>",
             argv[0]);

    fd = open(argv[1], O_RDONLY|O_BINARY);
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

    shadow = memalloc(MEM_SIZE);
    bmap = memalloc(MEM_SIZE/8);

    for (i = 0; i < argc; i++)
        printf("%s ", argv[i]);
    printf("\n");

    init_sigint_handler();

    amiga_insert_df0(argv[5]);
    amiga_init(&s, MEM_SIZE);
    regs = s.ctxt.regs;

    if (lseek(fd, off, SEEK_SET) != off)
        err(1, NULL);
    read_exact(fd, shadow, len);
    close(fd);

    /* Poison low-memory vectors. */
    for (i = 0; i < 0x100; i += 4)
        mem_write(i, 0xdeadbe00u | i, 4, &s);

    if (*argv[2] == '-') {
        /* Treat file as a loadable executable. Perform LoadSeg on it. */
        uint32_t *p = (uint32_t *)shadow;
        unsigned int i, j, k, nr_chunks, nr_longs, type, mem_off = base-4;
        unsigned int bptr = 0;
        if (be32toh(p[0]) != 0x3f3)
            errx(1, "Unexpected image signature %08x", be32toh(p[0]));
        printf("Loadable image: ");
        for (i = 1; p[i] != 0; i++)
            continue;
        nr_chunks = be32toh(p[i+1]);        
        printf("%u chunks\n", nr_chunks);
        i += 1 + 1 + 2 + nr_chunks;
        for (j = 0; j < nr_chunks; j++) {
            type = be32toh(p[i]);
            nr_longs = be32toh(p[i+1]) & 0x3fffffffu;
            printf("Chunk %u: %08x, %u longwords\n", j, type, nr_longs);
            i += 2;
            bptr = mem_off;
            mem_off += 4;
            if ((type == 0x3e9) || (type == 0x3ea)) {
                /* code/data */
                for (k = 0; k < nr_longs; k++) {
                    mem_write(mem_off, be32toh(p[i]), 4, &s);
                    i++;
                    mem_off += 4;
                }
            } else if (type == 0x3eb) {
                /* bss */
                for (k = 0; k < nr_longs; k++) {
                    mem_write(mem_off, 0, 4, &s);
                    mem_off += 4;
                }
            } else {
                errx(1, "Unexpected chunk type %08x", type);
            }
            if (be32toh(p[i]) != 0x3f2)
                errx(1, "Unexpected chunk end %08x", be32toh(p[i]));
            i++;
            mem_write(bptr, mem_off/4, 4, &s);
        }
        mem_write(bptr, 0, 4, &s);
    } else {
        /* Raw file. Load a portion of it into memory. */
        for (i = 0; i < len; i++)
            mem_write(base + i, shadow[i], 1, &s);
    }

    memset(shadow, 0, MEM_SIZE);

    regs->pc = base;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 1;

    mem_write(regs->a[7], 0xdeadbeee, 4, &s);

    while (!ctrl_c && (regs->pc != 0xdeadbeee)) {
        uint32_t pc = regs->pc;

        rc = amiga_emulate(&s);
        if (rc != M68KEMUL_OKAY)
            break;

        for (i = 0; i < s.ctxt.op_words; i++) {
            if ((pc + 2*i+1) >= MEM_SIZE)
                break;
            *(uint16_t *)&shadow[pc + 2*i] = htobe16(s.ctxt.op[i]);
            set_bit(pc + 2*i, bmap);
            set_bit(pc + 2*i+1, bmap);
        }
    }

    printf("%08x %04x %04x %04x %s\n", regs->pc,
           s.ctxt.op[0], s.ctxt.op[1],s.ctxt.op[2],s.ctxt.dis);
    m68k_dump_regs(regs, dump);
    m68k_dump_stack(&s.ctxt, stack_current, dump);

    for (i = 0; i < MEM_SIZE; i++)
        if (test_bit(i, bmap))
            mem_write(i, shadow[i], 1, &s);
    free(shadow);

    regs->pc = 0;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 0;

#define finish_zeroes_run() do {                        \
    if (zeroes_run >= 2) {                              \
        printf("      [%u more]\n", zeroes_run-1);      \
        printf("-------------------------------\n");    \
    }                                                   \
    zeroes_run = 0;                                     \
} while (0)

    while (regs->pc < (MEM_SIZE-2)) {
        uint32_t pc = regs->pc;

        rc = amiga_emulate(&s);

        if (!test_bit(pc, bmap)) {
            /*
             * If we are dumping non-executed bytes, check if we are decoding 
             * crap that overlaps a proper jump target. Truncate the crap if 
             * so!
             */
            for (i = 0; i < s.ctxt.op_words; i++)
                if (test_bit(pc+i*2, bmap))
                    s.ctxt.op_words = i;
#if 1
            /* Skip unexecuted stuff. */
            while (!test_bit(pc, bmap) && (pc < MEM_SIZE-2))
                pc += 2;
            regs->pc = pc;
            zeroes_run = 0;
            printf("-------------------------------\n");
            continue;
#endif
        }

        /* Skip runs of ori.b #0,d0 */
        if ((s.ctxt.op_words == 2) &&
            (s.ctxt.op[0] == 0) && (s.ctxt.op[1] == 0)) {
            if (++zeroes_run > 2)
                goto skip;
        } else {
            finish_zeroes_run();
        }

        /* Print an '*' for lines that were not actually executed. */
        printf("%08x %c", pc, test_bit(pc, bmap) ? ' ' : '*');

        if (zeroes_run == 2) {
            printf(".... .... ");
            goto skip;
        }

        for (i = 0; i < 3; i++) {
            if (i < s.ctxt.op_words)
                printf("%04x ", s.ctxt.op[i]);
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
        if (i < s.ctxt.op_words) {
            printf("%08x  ", pc + 2*i);
            while (i < s.ctxt.op_words)
                printf("%04x ", s.ctxt.op[i++]);
            printf("\n");
        }

    skip:
        regs->pc = pc + s.ctxt.op_words*2;
    }

    finish_zeroes_run();

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
