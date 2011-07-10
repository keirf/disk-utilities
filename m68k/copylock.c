/******************************************************************************
 * m68k/disassemble.c
 * 
 * Disassemble 680x0 code.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdarg.h>
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

#define MEM_SIZE (512*1024) /* our system has 512kB RAM */

static void read_exact(int fd, void *buf, size_t count)
{
    size_t done;

    while ( count > 0 )
    {
        done = read(fd, buf, count);
        if ( (done < 0) && ((errno == EAGAIN) || (errno == EINTR)) )
            done = 0;
        if ( done < 0 )
            err(1, NULL);
        count -= done;
    }
}

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

int main(int argc, char **argv)
{
    struct amiga_state s;
    struct m68k_regs *regs;
    char *p, *shadow, *bmap;
    int rc, i, fd, zeroes_run = 0;
    uint32_t off, len, base;

    if ( argc != 6 )
        errx(1, "Usage: %s <infile> <off> <len> <base> <df0_file>",
             argv[0]);

    fd = open(argv[1], O_RDONLY);
    if ( fd == -1 )
        err(1, "%s", argv[1]);

    off = strtol(argv[2], NULL, 16);
    len = strtol(argv[3], NULL, 16);
    base = strtol(argv[4], NULL, 16);

    if ( (base+len) > MEM_SIZE )
        errx(1, "Image cannot be loaded into %ukB RAM\n", MEM_SIZE>>10);

    if ( ((shadow = malloc(MEM_SIZE)) == NULL) ||
         ((bmap = malloc(MEM_SIZE/8)) == NULL) )
        err(1, NULL);

    for ( i = 0; i < argc; i++ )
        printf("%s ", argv[i]);
    printf("\n");

    amiga_insert_df0(argv[5]);
    amiga_init(&s, MEM_SIZE);
    regs = s.ctxt.regs;

    if ( lseek(fd, off, SEEK_SET) != off )
        err(1, NULL);
    read_exact(fd, shadow, len);
    close(fd);
    for ( i = 0; i < len; i++ )
        mem_write(base + i, shadow[i], 1, &s);

    memset(shadow, 0, MEM_SIZE);
    memset(bmap, 0, MEM_SIZE/8);

    regs->pc = base;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 1;

    for ( ; ; )
    {
        uint32_t pc = regs->pc;

        if ( pc == 0x76668 )
        {
            uint32_t v;
            mem_read(pc, &v, 2, &s);
            *(uint16_t *)&shadow[pc] = htons(v);
            set_bit(pc, bmap);
            break;
        }

        rc = amiga_emulate(&s);
        if ( rc != M68KEMUL_OKAY )
            break;

        for ( i = 0; i < s.ctxt.op_words; i++ )
        {
            if ( (pc + 2*i+1) >= MEM_SIZE )
                break;
            *(uint16_t *)&shadow[pc + 2*i] = htons(s.ctxt.op[i]);
            set_bit(pc + 2*i, bmap);
            set_bit(pc + 2*i+1, bmap);
        }
    }

    printf("%08x %04x %04x %04x %s\n", regs->pc,
           s.ctxt.op[0], s.ctxt.op[1],s.ctxt.op[2],s.ctxt.dis);
    m68k_dump_regs(regs, dump);
    m68k_dump_stack(&s.ctxt, stack_super, dump);

    for ( i = 0; i < MEM_SIZE; i++ )
        if ( test_bit(i, bmap) )
            mem_write(i, shadow[i], 1, &s);
    free(shadow);

    regs->pc = 0;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 0;

#define finish_zeroes_run() do {                        \
    if ( zeroes_run >= 2 ) {                            \
        printf("      [%u more]\n", zeroes_run-1);      \
        printf("-------------------------------\n");    \
    }                                                   \
    zeroes_run = 0;                                     \
} while (0)

    while ( regs->pc < (MEM_SIZE-2) )
    {
        uint32_t pc = regs->pc;

        rc = amiga_emulate(&s);

        if ( !test_bit(pc, bmap) )
        {
            /*
             * If we are dumping non-executed bytes, check if we are decoding 
             * crap that overlaps a proper jump target. Truncate the crap if 
             * so!
             */
            for ( i = 0; i < s.ctxt.op_words; i++ )
                if ( test_bit(pc+i*2, bmap) )
                    s.ctxt.op_words = i;
#if 0
            /*
             * If this is unexecuted and looks like crap, skip it and
             * everything else until we reach executed code.
             */
            if ( strchr(s.ctxt.dis, '?') )
            {
                while ( !test_bit(pc, bmap) && (pc < MEM_SIZE-2) )
                    pc += 2;
                regs->pc = pc;
                finish_zeroes_run();
                continue;
            }
#endif
        }

        /* Skip runs of ori.b #0,d0 */
        if ( (s.ctxt.op_words == 2) &&
             (s.ctxt.op[0] == 0) && (s.ctxt.op[1] == 0) )
        {
            if ( ++zeroes_run > 2 )
                goto skip;
        }
        else
        {
            finish_zeroes_run();
        }

        /* Print an '*' for lines that were not actually executed. */
        printf("%08x %c", pc, test_bit(pc, bmap) ? ' ' : '*');

        if ( zeroes_run == 2 )
        {
            printf(".... .... ");
            goto skip;
        }

        for ( i = 0; i < 3; i++ )
        {
            if ( i < s.ctxt.op_words )
                printf("%04x ", s.ctxt.op[i]);
            else
                printf("     ");
        }
        if ( (p = strchr(s.ctxt.dis, '\t')) != NULL )
            *p = '\0';
        printf(" %s", s.ctxt.dis);
        if ( p )
        {
            int spaces = 8-(p-s.ctxt.dis);
            if ( spaces < 1 )
                spaces = 1;
            printf("%*s%s", spaces, "", p+1);
        }
        printf("\n");
        if ( i < s.ctxt.op_words )
        {
            printf("%08x  ", pc + 2*i);
            while ( i < s.ctxt.op_words )
                printf("%04x ", s.ctxt.op[i++]);
            printf("\n");
        }

    skip:
        regs->pc = pc + s.ctxt.op_words*2;
    }

    finish_zeroes_run();

    return 0;
}
