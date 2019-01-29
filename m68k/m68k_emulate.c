/*
 * m68k/m68k_emulate.c
 * 
 * M68000 emulator wrapper.
 * 
 * Written in 2019 by Keir Fraser
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

#include <libdisk/util.h>
#include <m68k/m68k_emulate.h>

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

static uint32_t mem_size;

struct m68k_state {
    struct m68k_emulate_ctxt ctxt;
    char addr_name[16];
    char *mem;
};

static int emul_read(uint32_t addr, uint32_t *val, unsigned int bytes,
                     struct m68k_emulate_ctxt *ctxt)
{
    struct m68k_state *s = container_of(ctxt, struct m68k_state, ctxt);

    if ((addr > mem_size) || ((addr+bytes) > mem_size))
        return M68KEMUL_UNHANDLEABLE;

    switch (bytes) {
    case 1:
        *val = *(uint8_t *)&s->mem[addr];
        break;
    case 2:
        *val = be16toh(*(uint16_t *)&s->mem[addr]);
        break;
    case 4:
        *val = be32toh(*(uint32_t *)&s->mem[addr]);
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

    if ((addr > mem_size) || ((addr+bytes) > mem_size))
        return M68KEMUL_UNHANDLEABLE;

    switch (bytes) {
    case 1:
        *(uint8_t *)&s->mem[addr] = val;
        break;
    case 2:
        *(uint16_t *)&s->mem[addr] = htobe16(val);
        break;
    case 4:
        *(uint32_t *)&s->mem[addr] = htobe32(val);
        break;
    default:
        return M68KEMUL_UNHANDLEABLE;
    }

    return M68KEMUL_OKAY;
}


static struct m68k_emulate_ops emul_ops = {
    .read = emul_read,
    .write = emul_write,
};

int main(int argc, char **argv)
{
    struct m68k_state s = { { 0 } };
    struct m68k_regs regs = { { 0 } };
    uint32_t f_sz, r[20], *_r;
    FILE *fd;
    int rc, i;

    if (argc != 3)
        errx(1, "Usage: %s <in_statefile> <out_statefile>", argv[0]);

    fd = fopen(argv[1], "rb");
    if (fd == NULL)
        err(1, "%s", argv[1]);

    if (fseek(fd, 0, SEEK_END) != 0)
        err(1, "%s", argv[1]);
    f_sz = ftell(fd);
    if (fseek(fd, 0, SEEK_SET) != 0)
        err(1, "%s", argv[1]);

    mem_size = f_sz - 20*4;
    s.mem = malloc(mem_size);

    /* Unmarshal memory. */
    if (fread(s.mem, mem_size, 1, fd) != 1)
        err(1, "%s", argv[1]);

    /* Unmarshal registers, 80 bytes: '>18IH6x' % (d0-d7,a0-a7,pc,ssp,sr) */
    if (fread(r, 4, 20, fd) != 20)
        err(1, "%s", argv[1]);
    _r = (uint32_t *)&regs;
    for (i = 0; i < 18; i++)
        *_r++ = be32toh(r[i]);
    regs.sr = be16toh(*(uint16_t *)&r[i]);

    fclose(fd);

    init_sigint_handler();

    s.ctxt.regs = &regs;
    s.ctxt.ops = &emul_ops;
    s.ctxt.disassemble = 1;
    s.ctxt.emulate = 1;

    while (!ctrl_c && (regs.pc < mem_size)) {
        rc = m68k_emulate(&s.ctxt);
        if (rc != M68KEMUL_OKAY)
            break;
    }

    fd = fopen(argv[2], "wb");
    if (fd == NULL)
        err(1, "%s", argv[2]);

    /* Marshal memory. */
    if (fwrite(s.mem, mem_size, 1, fd) != 1)
        err(1, "%s", argv[1]);

    /* Marshal registers. */
    memset(r, 0, sizeof(r));
    _r = (uint32_t *)&regs;
    for (i = 0; i < 18; i++)
        r[i] = htobe32(*_r++);
    *(uint16_t *)&r[i] = htobe16(regs.sr);
    if (fwrite(r, 4, 20, fd) != 20)
        err(1, "%s", argv[1]);

    fclose(fd);

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
