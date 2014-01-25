/*
 * m68k_emulate.c
 * 
 * Emulate/disassemble m680x0 opcodes.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "m68k_emulate.h"

/* Type, address-of, and value of an instruction's operand. */
struct operand {
    enum { OP_REG, OP_MEM, OP_IMM, OP_SR } type;

    uint32_t val;

    /* OP_REG: Pointer to register field. */
    uint32_t *reg;

    /* OP_MEM: Emulated virtual address. */
    uint32_t mem;
};

struct m68k_emulate_priv_ctxt {
    char *dis_p; /* ptr into dis[] char buffer */
    struct m68k_regs sh_regs; /* shadow copy of regs before writeback */
    struct operand operand;
    struct m68k_exception exception;
};

/* SR flags */
#define SR_T (1u<<15)
#define SR_S (1u<<13)

/* Condition code flags */
#define CC_C (1u<<0)
#define CC_V (1u<<1)
#define CC_Z (1u<<2)
#define CC_N (1u<<3)
#define CC_X (1u<<4)

/* Internal return codes */
#define M68KEMUL_SKIP_EMULATION 16

/* Helper macro to avoid cumbersome if() stmts everywhere. */
#define bail_if(_op) do { if ((_op) != 0) goto bail; } while (0)

/* Helper macro to pick a shadow register value. */
#define sh_reg(c,n) ((c)->p->sh_regs.n)

#define raise_exception(vec) do {               \
    c->p->exception.vector = vec;               \
    rc = M68KEMUL_EXCEPTION;                    \
    goto bail;                                  \
} while (0)

#define raise_exception_if(p, vec) do {         \
    if (p) raise_exception(vec);                \
} while (0)

static const char op_sz_ch[] = { 'b', 'w', 'l', '?' };
static const char *dreg[] = {
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7" };
static const char *areg[] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "sp" };

enum access_type { access_fetch, access_read, access_write };
static int check_addr_align(
    struct m68k_emulate_ctxt *c, uint32_t addr, unsigned int bytes,
    enum access_type access_type)
{
    uint16_t sw = 0;
    if ((bytes == 1) || !(addr & 1))
        return M68KEMUL_OKAY;
    /*
     * Status word:
     *  [15:5]: unused
     *  [4]:    1=read 0=write
     *  [3]:    1=data 0=insn
     *  [2]:    1=super 0=user (Function Code bit 2)
     *  [1]:    1=insn 0=data  (Function Code bit 1)
     *  [0]:    1=data 0=insn  (Function Code bit 0)
     */
    sw  = access_type == access_write ? 0x00 : 0x10;
    sw |= access_type == access_fetch ? 0x02 : 0x09;
    if (c->regs->sr & SR_S)
        sw |= 0x04;
    c->p->exception.status_word = sw;
    c->p->exception.fault_addr = addr;
    c->p->exception.vector = M68KVEC_addr_error;
    return M68KEMUL_EXCEPTION;
}

#define raise_addr_err_if(p, a, b, t) do {              \
    if (p) { rc = raise_addr_err(c, a, t); goto bail; } \
} while (0)

static void acct_cycles_for_mem_access(
    struct m68k_emulate_ctxt *c, unsigned int bytes)
{
    c->cycles += (bytes == 4) ? 8 : 4;
}

static int fetch(
    uint32_t *val, unsigned int bytes,
    struct m68k_emulate_ctxt *c)
{
    uint32_t b, v;
    int rc;

    bail_if(rc = check_addr_align(c, sh_reg(c, pc), bytes, access_fetch));

    /* Invalidate prefetch queue if it is fetched from wrong address. */
    if (sh_reg(c, pc) != c->prefetch_addr)
        c->prefetch_valid = 0;

    /* Take as many words from the prefetch queue as possible. */
    for (b = v = 0; (b < bytes) && c->prefetch_valid; b += 2) {
        v = (v << 16) | c->prefetch_dat[0];
        c->prefetch_dat[0] = c->prefetch_dat[1];
        c->prefetch_addr += 2;
        c->prefetch_valid--;
    }

    /* Read remaining words from memory. */
    *val = 0;
    if (b != bytes)
        bail_if(rc = c->ops->read(sh_reg(c, pc) + b, val, bytes - b, c));

    /* Merge the result and do accounting. */
    *val |= v << (8 * (bytes - b));
    acct_cycles_for_mem_access(c, bytes);
    sh_reg(c, pc) += bytes;

    /* Re-fill the prefetch queue. */
    if (c->prefetch_valid == 0)
        c->prefetch_addr = sh_reg(c, pc);
    while (c->prefetch_valid != 2) {
        if (c->ops->read(c->prefetch_addr + c->prefetch_valid*2, &v, 2, c))
            break;
        c->prefetch_dat[c->prefetch_valid++] = (uint16_t)v;
    }

bail:
    return rc;
}

static int read(
    uint32_t addr, uint32_t *val, unsigned int bytes,
    struct m68k_emulate_ctxt *c)
{
    int rc;
    if (!c->emulate)
        return M68KEMUL_SKIP_EMULATION;
    bail_if(rc = check_addr_align(c, addr, bytes, access_read));
    bail_if(rc = c->ops->read(addr, val, bytes, c));
    acct_cycles_for_mem_access(c, bytes);
bail:
    return rc;
}

static int write(
    uint32_t addr, uint32_t val, unsigned int bytes,
    struct m68k_emulate_ctxt *c)
{
    int rc;
    if (!c->emulate)
        return M68KEMUL_SKIP_EMULATION;
    bail_if(rc = check_addr_align(c, addr, bytes, access_write));
    bail_if(rc = c->ops->write(addr, val, bytes, c));
    acct_cycles_for_mem_access(c, bytes);
bail:
    return rc;
}

static const char *addr_name(struct m68k_emulate_ctxt *c, uint32_t addr)
{
    if (!c->disassemble || !c->ops->addr_name)
        return NULL;
    return c->ops->addr_name(addr, c);
}

static int fetch_insn_word(struct m68k_emulate_ctxt *c, uint16_t *word)
{
    uint32_t val;
    int rc = fetch(&val, 2, c);
    c->op[c->op_words++] = *word = val;
    return rc;
}

#define _fetch_insn_bytes(s,t)                                  \
static int fetch_insn_##s##bytes(struct m68k_emulate_ctxt *c,   \
                                 t##int32_t *imm, uint8_t sz)   \
{                                                               \
    int rc = 0;                                                 \
                                                                \
    switch (sz) {                                               \
    case OPSZ_B:                                                \
        rc = fetch((uint32_t *)imm, 2, c);                      \
        c->op[c->op_words++] = *imm;                            \
        *imm = (t##int8_t)*imm;                                 \
        break;                                                  \
    case OPSZ_W:                                                \
        rc = fetch((uint32_t *)imm, 2, c);                      \
        c->op[c->op_words++] = *imm;                            \
        *imm = (t##int16_t)*imm;                                \
        break;                                                  \
    case OPSZ_L:                                                \
        rc = fetch((uint32_t *)imm, 4, c);                      \
        c->op[c->op_words++] = *imm>>16;                        \
        c->op[c->op_words++] = *imm;                            \
        break;                                                  \
    default:                                                    \
        rc = M68KEMUL_UNHANDLEABLE;                             \
        break;                                                  \
    }                                                           \
                                                                \
    return rc;                                                  \
}
_fetch_insn_bytes(s,)
_fetch_insn_bytes(u,u)

static void dump(struct m68k_emulate_ctxt *c, const char *fmt, ...)
{
    va_list args;

    if (!c->disassemble)
        return;

    va_start(args, fmt);
    c->p->dis_p += vsprintf(c->p->dis_p, fmt, args);
    va_end(args);
}

static int deliver_exception(struct m68k_emulate_ctxt *c)
{
    if (c->ops->deliver_exception)
        return c->ops->deliver_exception(c, &c->p->exception);
    return m68k_deliver_exception(c, &c->p->exception);
}

static void update_sr(struct m68k_emulate_ctxt *c, uint16_t new_sr)
{
    uint16_t old_sr = sh_reg(c, sr);
    if ((old_sr ^ new_sr) & SR_S) {
        uint32_t xsp = sh_reg(c, a[7]);
        sh_reg(c, a[7]) = sh_reg(c, xsp);
        sh_reg(c, xsp) = xsp;
    }
    sh_reg(c, sr) = new_sr;
}

static void cc_mov(struct m68k_emulate_ctxt *c, uint32_t result)
{
    uint16_t sr = sh_reg(c, sr);
    sr &= ~(CC_N|CC_Z|CC_V|CC_C);
    if (c->op_sz == OPSZ_W)
        result = (int16_t)result;
    else if (c->op_sz == OPSZ_B)
        result = (int8_t)result;
    sr |= result & (1u<<31) ? CC_N : 0;
    sr |= result == 0 ? CC_Z : 0;
    sh_reg(c, sr) = sr;
}

static int cc_eval_condition(struct m68k_emulate_ctxt *c, uint8_t cond)
{
    uint8_t cc = c->regs->sr;
    int r = 0;

    switch ((cond >> 1) & 7) {
    case 0: r = 1; break;
    case 1: r = !(cc & CC_C) && !(cc & CC_Z); break;
    case 2: r = !(cc & CC_C); break;
    case 3: r = !(cc & CC_Z); break;
    case 4: r = !(cc & CC_V); break;
    case 5: r = !(cc & CC_N); break;
    case 6:
        r = ((cc & (CC_N|CC_V)) == (CC_N|CC_V)) || !(cc & (CC_N|CC_V));
        break;
    case 7:
        r = ((cc & (CC_N|CC_V|CC_Z)) == (CC_N|CC_V))
            || !(cc & (CC_N|CC_V|CC_Z));
        break;
    }

    return (cond & 1) ? !r : r;
}

static int decode_ea(struct m68k_emulate_ctxt *c)
{
    struct operand *op = &c->p->operand;
    const char *name;
    uint8_t mode, reg;
    int rc = 0;

    op->type = OP_MEM; /* most common */
    mode = (c->op[0] >> 3) & 7;
    reg = c->op[0] & 7;

    switch (mode) {
    case 0:
        op->type = OP_REG;
        op->reg = &sh_reg(c, d[reg]);
        dump(c, "%s", dreg[reg]);
        break;
    case 1:
        op->type = OP_REG;
        op->reg = &sh_reg(c, a[reg]);
        dump(c, "%s", areg[reg]);
        break;
    case 2:
        op->reg = &sh_reg(c, a[reg]);
        op->mem = *op->reg;
        if ((name = addr_name(c, op->mem)) != NULL)
            dump(c, "%s", name);
        dump(c, "(%s)", areg[reg]);
        break;
    case 3:
        op->reg = &sh_reg(c, a[reg]);
        op->mem = *op->reg;
        *op->reg += (c->op_sz == OPSZ_B ? 1 : c->op_sz == OPSZ_W ? 2 : 4);
        if ((reg == 7) && (c->op_sz == OPSZ_B))
            (*op->reg)++; /* keep sp word-aligned */
        dump(c, "(%s)+", areg[reg]);
        break;
    case 4:
        op->reg = &sh_reg(c, a[reg]);
        op->mem = *op->reg -=
            (c->op_sz == OPSZ_B ? 1 : c->op_sz == OPSZ_W ? 2 : 4);
        if ((reg == 7) && (c->op_sz == OPSZ_B))
            op->mem = --(*op->reg); /* keep sp word-aligned */
        dump(c, "-(%s)", areg[reg]);
        break;
    case 5: {
        int32_t disp;
        bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_W));
        op->mem = sh_reg(c, a[reg]) + disp;
        if ((name = addr_name(c, op->mem)) != NULL)
            dump(c, "%s", name);
        else if (disp < 0)
            dump(c, "-%x", -disp);
        else
            dump(c, "%x", disp);
        dump(c, "(%s)", areg[reg]);
        break;
    }
    case 6: {
        uint16_t ext;
        bail_if(rc = fetch_insn_word(c, &ext));
        if (!(ext & (1u << 8))) {
            int32_t idx = (ext & (1u<<15) ? sh_reg(c,a) : sh_reg(c,d))[
                (ext>>12)&7];
            int8_t disp = (int8_t)ext;
            if (!(ext & (1u<<11)))
                idx = (int16_t)idx;
            idx <<= (ext>>9)&3;
            op->mem = sh_reg(c, a[reg]) + disp + idx;
            if (disp < 0) {
                dump(c, "-");
                disp = -disp;
            }
            dump(c, "%x(%s,%s.%c*%u)", disp, areg[reg],
                 (ext & (1u<<15) ? areg : dreg)[(ext>>12)&7],
                 ext & (1u<<11) ? 'l' : 'w', 1u << ((ext>>9)&3));
        } else {
            dump(c, "???[68020+]");
            rc = M68KEMUL_UNHANDLEABLE;
        }
        break;
    }
    case 7: {
        switch (reg) {
        case 0:
            bail_if(rc = fetch_insn_ubytes(c, &op->mem, OPSZ_W));
        abs_addr:
            if ((name = addr_name(c, op->mem)) != NULL)
                dump(c, "%s", name);
            else
                dump(c, "%x", op->mem);
            break;
        case 1:
            bail_if(rc = fetch_insn_ubytes(c, &op->mem, OPSZ_L));
            goto abs_addr;
        case 2: {
            int32_t disp;
            op->mem = sh_reg(c, pc);
            bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_W));
            op->mem += disp;
            dump(c, "%x(pc)", op->mem);
            break;
        }
        case 3: {
            uint32_t target = sh_reg(c, pc);
            uint16_t ext;
            bail_if(rc = fetch_insn_word(c, &ext));
            target += (int8_t)ext;
            if (!(ext & (1u << 8))) {
                int32_t idx = (ext & (1u<<15) ? sh_reg(c,a) : sh_reg(c,d))[
                    (ext>>12)&7];
                if (!(ext & (1u<<11)))
                    idx = (int16_t)idx;
                idx <<= (ext>>9)&3;
                op->mem = target + idx;
                dump(c, "%04x(pc,%s.%c*%u)", target,
                     (ext & (1u<<15) ? areg : dreg)[(ext>>12)&7],
                     ext & (1u<<11) ? 'l' : 'w', 1u << ((ext>>9)&3));
            } else {
                dump(c, "???[68020+]");
                rc = M68KEMUL_UNHANDLEABLE;
            }
            break;
        }
        case 4:
            op->type = OP_IMM;
            bail_if(rc = fetch_insn_ubytes(c, &op->val, c->op_sz));
            dump(c, "#%x", op->val);
            break;
        default:
            dump(c, "???");
            raise_exception(M68KVEC_illegal_insn);
            break;
        }
    }
    }

bail:
    return rc;
}

static int decode_mem_ea(struct m68k_emulate_ctxt *c)
{
    int rc = decode_ea(c);
    if ((rc == 0) && (c->p->operand.type != OP_MEM))
        rc = M68KEMUL_UNHANDLEABLE;
    return rc;
}

static int read_ea(struct m68k_emulate_ctxt *c)
{
    struct operand *op = &c->p->operand;
    unsigned int bytes;
    int rc = 0;

    bytes = ((c->op_sz == OPSZ_B) ? 1 :
             (c->op_sz == OPSZ_W) ? 2 :
             (c->op_sz == OPSZ_L) ? 4 : 0);
    if (!bytes)
        return M68KEMUL_UNHANDLEABLE;

    switch (op->type) {
    case OP_MEM:
        rc = read(op->mem, &op->val, bytes, c);
        break;
    case OP_REG:
        op->val = *op->reg;
        if (bytes == 1)
            op->val = (uint8_t)op->val;
        else if (bytes == 2)
            op->val = (uint16_t)op->val;
        break;
    case OP_IMM:
        /* already in op->val */
        break;
    case OP_SR:
        op->val = sh_reg(c, sr);
        if (bytes == 1)
            op->val = (uint8_t)op->val;
        break;
    }

    return rc;
}

static int write_ea(struct m68k_emulate_ctxt *c)
{
    struct operand *op = &c->p->operand;
    unsigned int bytes;
    int rc = 0;

    bytes = ((c->op_sz == OPSZ_B) ? 1 :
             (c->op_sz == OPSZ_W) ? 2 :
             (c->op_sz == OPSZ_L) ? 4 : 0);
    if (!bytes)
        return M68KEMUL_UNHANDLEABLE;

    switch (op->type) {
    case OP_MEM:
        rc = write(op->mem, op->val, bytes, c);
        break;
    case OP_REG:
        *op->reg = (bytes == 1 ? (*op->reg & ~0xffu) | (uint8_t)op->val :
                    bytes == 2 ? (*op->reg & ~0xffffu) | (uint16_t)op->val :
                    op->val);
        break;
    case OP_SR:
        if (bytes == 1)
            sh_reg(c, sr) = (sh_reg(c, sr) & ~0xffu) | (uint8_t)op->val;
        else
            update_sr(c, op->val);
        break;
    default:
        rc = M68KEMUL_UNHANDLEABLE;
        break;
    }

    return rc;
}

static uint32_t _op_sub(struct m68k_emulate_ctxt *c, uint32_t s, uint32_t d)
{
    uint32_t msb, r;
    uint16_t sr;

    msb = 1u << (c->op_sz == OPSZ_L ? 31 : c->op_sz == OPSZ_W ? 15 : 7);
    r = d - s;

    sr = sh_reg(c, sr) & ~(CC_N|CC_Z|CC_V|CC_C);
    if (r & msb)
        sr |= CC_N;
    if ((r & ((msb<<1)-1)) == 0)
        sr |= CC_Z;
    if (((s ^ d) & msb) && ((d ^ r) & msb))
        sr |= CC_V;
    if ((s & ~d & msb) || (r & ~d & msb) || (s & r & msb))
        sr |= CC_C;
    sh_reg(c, sr) = sr;

    return r;
}

static void op_cmp(struct m68k_emulate_ctxt *c, uint32_t s, uint32_t d)
{
    (void)_op_sub(c, s, d);
}

static int op_sub(struct m68k_emulate_ctxt *c, uint32_t s)
{
    c->p->operand.val = _op_sub(c, s, c->p->operand.val);
    sh_reg(c, sr) &= ~CC_X;
    if (sh_reg(c, sr) & CC_C)
        sh_reg(c, sr) |= CC_X;
    return write_ea(c);
}

static int op_add(struct m68k_emulate_ctxt *c, uint32_t s)
{
    uint32_t msb, d, r;
    uint16_t sr;

    msb = 1u << (c->op_sz == OPSZ_L ? 31 : c->op_sz == OPSZ_W ? 15 : 7);
    d = c->p->operand.val;
    r = d + s;

    sr = sh_reg(c, sr) & ~(CC_X|CC_N|CC_Z|CC_V|CC_C);
    if (r & msb)
        sr |= CC_N;
    if ((r & ((msb<<1)-1)) == 0)
        sr |= CC_Z;
    if (!((s ^ d) & msb) && ((d ^ r) & msb))
        sr |= CC_V;
    if ((s & d & msb) || (s & ~r & msb) || (d & ~r & msb))
        sr |= CC_C | CC_X;
    sh_reg(c, sr) = sr;

    c->p->operand.val = r;
    return write_ea(c);
}

static int misc_insn(struct m68k_emulate_ctxt *c)
{
    uint16_t op = c->op[0];
    int rc = 0;

    /*
     * Misc instruction category (op[15:12]=4) is a hotch potch.
     * Prevent mis-decoding by checking most precise matches first.
     */

    /* 1. Simple full opcode matches. */
    if (op == 0x4afau) {
        /* bgnd */
        dump(c, "bgnd");
        rc = M68KEMUL_UNHANDLEABLE;
    } else if (op == 0x4afcu) {
        /* illegal */
        dump(c, "illegal");
        raise_exception(M68KVEC_illegal_insn);
    } else if (op == 0x4e70u) {
        /* reset */
        dump(c, "reset");
        rc = M68KEMUL_UNHANDLEABLE;
    } else if (op == 0x4e71u) {
        /* nop */
        dump(c, "nop");
    } else if (op == 0x4e72u) {
        /* stop */
        uint16_t data;
        bail_if(rc = fetch_insn_word(c, &data));
        dump(c, "stop\t#%x", data);
        raise_exception_if(!(sh_reg(c, sr) & SR_S), M68KVEC_priv_violation);
        update_sr(c, data);
        /* should wait for an interrupt/exception... */
    } else if (op == 0x4e73u) {
        /* rte */
        uint32_t new_pc, new_sr;
        dump(c, "rte");
        raise_exception_if(!(sh_reg(c, sr) & SR_S), M68KVEC_priv_violation);
        bail_if(rc = read(sh_reg(c, a[7]) + 2, &new_pc, 4, c));
        bail_if(rc = read(sh_reg(c, a[7]) + 0, &new_sr, 2, c));
        sh_reg(c, a[7]) += 6;
        update_sr(c, new_sr);
        sh_reg(c, pc) = new_pc;
    } else if (op == 0x4e74u) {
        /* rtd */
        int32_t disp;
        bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_W));
        dump(c, "rtd\t#");
        if (disp < 0)
            dump(c, "-%x", -disp);
        else
            dump(c, "%x", disp);
        bail_if(rc = read(sh_reg(c,a[7]), &sh_reg(c, pc), 4, c));
        sh_reg(c, a[7]) += 4 + disp;
    } else if (op == 0x4e75u) {
        /* rts */
        dump(c, "rts");
        bail_if(rc = read(sh_reg(c,a[7]), &sh_reg(c, pc), 4, c));
        sh_reg(c, a[7]) += 4;
    } else if (op == 0x4e76u) {
        /* trapv */
        dump(c, "trapv");
        raise_exception_if(sh_reg(c, sr) & CC_V, M68KVEC_trapcc_trapv);
    } else if (op == 0x4e77u) {
        /* rtr */
        uint32_t new_pc, new_sr;
        dump(c, "rtr");
        bail_if(rc = read(sh_reg(c, a[7]) + 2, &new_pc, 4, c));
        bail_if(rc = read(sh_reg(c, a[7]) + 0, &new_sr, 2, c));
        sh_reg(c, a[7]) += 6;
        sh_reg(c, sr) &= ~0xffu;
        sh_reg(c, sr) |= (uint8_t)new_sr;
        sh_reg(c, pc) = new_pc;
    }

    /* 2. Exact matches with no invalid cases. */
    else if ((op & 0xfff8u) == 0x4840u) {
        /* swap */
        uint32_t *reg = &sh_reg(c, d[op&7]);
        c->op_sz = OPSZ_L;
        dump(c, "swap\t%s", dreg[op&7]);
        *reg = (*reg << 16) | (uint16_t)(*reg >> 16);
        cc_mov(c, *reg);
    } else if ((op & 0xfff8u) == 0x4848u) {
        /* bkpt */
        dump(c, "bkpt\t#%x", op&7);
        rc = M68KEMUL_UNHANDLEABLE;
    } else if ((op & 0xfff8u) == 0x49c0u) {
        /* extb */
        uint32_t *reg = &sh_reg(c, d[op&7]);
        c->op_sz = OPSZ_L;
        dump(c, "extb.%c\t%s", op_sz_ch[c->op_sz], dreg[op&7]);
        *reg = (int8_t)*reg;
        cc_mov(c, *reg);
    } else if (((op & 0xfff8u) == 0x4e50u) || ((op & 0xfff8u) == 0x4808u)) {
        /* link */
        int32_t disp;
        uint32_t *reg = &sh_reg(c, a[op&7]);
        c->op_sz = op & (1u<<3) ? OPSZ_L : OPSZ_W;
        bail_if(rc = fetch_insn_sbytes(c, &disp, c->op_sz));
        dump(c, "link.%c\t%s,#",
             op_sz_ch[c->op_sz], areg[op&7]);
        if (disp < 0)
            dump(c, "-");
        dump(c, "%x", (disp < 0) ? -disp : disp);
        sh_reg(c, a[7]) -= 4;
        bail_if(rc = write(sh_reg(c, a[7]), *reg, 4, c));
        *reg = sh_reg(c, a[7]);
        sh_reg(c, a[7]) += disp;
    } else if ((op & 0xfff8u) == 0x4e58u) {
        /* unlk */
        uint32_t *reg = &sh_reg(c, a[op&7]);
        dump(c, "unlk\t%s", areg[op&7]);
        sh_reg(c, a[7]) = *reg;
        bail_if(rc = read(sh_reg(c, a[7]), reg, 4, c));
        sh_reg(c, a[7]) += 4;
    }

    /*
     * 3. All the rest. The matches may be approximate, and include
     * invalid cases for the matched instruction. Where that matters, we should
     * have already decoded the correct instruction with a more precise match.
     */
    else if ((op & 0xf140u) == 0x4100u) {
        /* chk */
        c->op_sz = (op & (1u<<7)) ? OPSZ_W : OPSZ_L;
        dump(c, "chk.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        dump(c, ",%s", dreg[(op >> 9) & 7]);
        rc = M68KEMUL_UNHANDLEABLE;
    } else if (((op & 0xff00u) == 0x4200u) &&
               ((c->op_sz = (op>>6)&3) != OPSZ_X)) {
        /* clr */
        dump(c, "clr.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        c->p->operand.val = 0;
        bail_if(rc = write_ea(c));
        sh_reg(c, sr) &= ~(CC_N|CC_Z|CC_V|CC_C);
        sh_reg(c, sr) |= CC_Z;
    } else if ((op & 0xffc0u) == 0x4c40u) {
        /* divs/divu.l */
        uint16_t ext, dr, dq, sz;
        bail_if(rc = fetch_insn_word(c, &ext));
        dr = ext&7; dq = (ext>>12)&7; sz = (ext>>10)&1;
        c->op_sz = OPSZ_L;
        dump(c, "div%c", ext & (1u<<11) ? 's' : 'u');
        if (!sz && (dr != dq))
            dump(c, "l");
        dump(c, ".l\t");
        bail_if(rc = decode_ea(c));
        dump(c, ",");
        if (sz || (dr != dq))
            dump(c, "%s:", dreg[dr]);
        dump(c, "%s", dreg[dq]);
        rc = M68KEMUL_UNHANDLEABLE;
    } else if ((op & 0xffb8u) == 0x4880u) {
        /* ext */
        uint32_t *reg = &sh_reg(c, d[op&7]);
        c->op_sz = (op & (1u<<6)) ? OPSZ_L : OPSZ_W;
        dump(c, "ext.%c\t%s", op_sz_ch[c->op_sz], dreg[op&7]);
        *reg = (c->op_sz == OPSZ_W
                ? (*reg & ~0xffffu) | (uint16_t)(int8_t)*reg
                : (int16_t)*reg);
        cc_mov(c, *reg);
    } else if ((op & 0xff80u) == 0x4e80u) {
        /* jmp/jsr */
        dump(c, "j%s\t", (op & (1u<<6)) ? "mp" : "sr");
        bail_if(rc = decode_mem_ea(c));
        if (!(op & (1u<<6))) {
            /* push return address (current pc) */
            sh_reg(c, a[7]) -= 4;
            bail_if(rc = write(sh_reg(c, a[7]), sh_reg(c, pc), 4, c));
        }
        /* update pc to jump target */
        sh_reg(c, pc) = c->p->operand.mem;
    } else if ((op & 0xf1c0u) == 0x41c0u) {
        /* lea */
        c->op_sz = OPSZ_L;
        dump(c, "lea.l\t");
        bail_if(rc = decode_mem_ea(c));
        dump(c, ",%s", areg[(op>>9)&7]);
        sh_reg(c, a[(op>>9)&7]) = c->p->operand.mem;
    } else if ((op & 0xfdc0u) == 0x40c0u) {
        /* move from ccr/sr */
        c->op_sz = OPSZ_W;
        dump(c, "move.w\t%s,", op & (1u<<9) ? "ccr" : "sr");
        bail_if(rc = decode_ea(c));
        c->p->operand.val = sh_reg(c, sr);
        if (op & (1u<<9))
            c->p->operand.val = (uint8_t)c->p->operand.val;
        bail_if(rc = write_ea(c));
    } else if ((op & 0xfdc0u) == 0x44c0u) {
        /* move to ccr/sr */
        c->op_sz = OPSZ_W;
        dump(c, "move.w\t");
        bail_if(rc = decode_ea(c));
        dump(c, ",%s", op & (1u<<9) ? "sr" : "ccr");
        bail_if(rc = read_ea(c));
        if (op & (1u<<9)) {
            raise_exception_if(!(sh_reg(c, sr) & SR_S),
                               M68KVEC_priv_violation);
            update_sr(c, c->p->operand.val);
        } else {
            sh_reg(c, sr) &= ~0xffu;
            sh_reg(c, sr) |= (uint8_t)c->p->operand.val;
        }
    } else if ((op & 0xfff0u) == 0x4e60u) {
        /* move to/from usp */
        c->op_sz = OPSZ_L;
        dump(c, "move.l\t");
        dump(c, op&(1u<<3) ? "usp,%s" : "%s,usp", areg[op&7]);
        raise_exception_if(!(sh_reg(c, sr) & SR_S), M68KVEC_priv_violation);
        if (op & (1u<<3))
            sh_reg(c, a[op&7]) = sh_reg(c, xsp);
        else
            sh_reg(c, xsp) = sh_reg(c, a[op&7]);
    } else if ((op & 0xfffeu) == 0x4e7au) {
        /* movec */
        const static char *creg[] = {
            "sfc", "dfc", "cacr", "tc", "itt0", "itt1", "dtt0", "dtt1",
            "usp", "vbr", "caar", "msp", "isp", "mmusr", "urp", "srp" };
        uint16_t ext, idx;
        const char *greg;
        bail_if(rc = fetch_insn_word(c, &ext));
        idx = ext & 0x0fffu;
        greg = (ext & (1u<<15) ? areg : dreg)[(ext>>12)&7];
        if (((idx > 7) && (idx < 0x800u)) || (idx > 0x807u))
            goto unknown; /* bad creg */
        idx = (idx & 7) | ((idx & 0x800u) >> 8);
        c->op_sz = OPSZ_L;
        dump(c, "movec.l\t");
        if (op & 1)
            dump(c, "%s,%s", greg, creg[idx]);
        else
            dump(c, "%s,%s", creg[idx], greg);
        raise_exception(M68KVEC_illegal_insn);
    } else if ((op & 0xfb80u) == 0x4880u) {
        /* movem */
        uint32_t mask, *r;
        int reg, predec = ((op & 0x38u) == 0x20u);
        c->op_sz = op & (1u<<6) ? OPSZ_L : OPSZ_W;
        bail_if(rc = fetch_insn_ubytes(c, &mask, OPSZ_W));
        dump(c, "movem.%c\t", op_sz_ch[c->op_sz]);
        if (op & (1u<<10)) {
            bail_if(rc = decode_ea(c));
            dump(c, ",");
        }
        if (predec) {
            /* flip the mask for predec addr mode */
            uint16_t tmp = mask;
            for (reg=0, mask=0; reg < 16; reg++, tmp >>= 1)
                mask = (mask << 1) | (tmp & 1);
        }
        for (reg = 0; reg < 16; reg++) {
            if (!(mask & (1u<<reg)))
                continue;
            if (!(reg & 7) || !(mask & (1u<<(reg-1)))) {
                if (mask & ((1u<<reg)-1))
                    dump(c, "/");
                dump(c, "%s", ((reg&8)?areg:dreg)[reg&7]);
            } else if (((reg&7)==7) || !(mask & (2u<<reg))) {
                if (mask & (1u<<(reg-1)))
                    dump(c, "-");
                dump(c, "%s", ((reg&8)?areg:dreg)[reg&7]);
            }
        }
        if (!(op & (1u<<10))) {
            dump(c, ",");
            bail_if(rc = decode_ea(c));
        }
        /* Now the actual movem emulation... */
        for (!predec ? (reg = 0) : (reg = 15);
             !predec ? reg < 16  : reg >= 0;
             !predec ? reg++     : reg--) {
            if (!(mask & (1u<<reg)))
                continue;
            r = (reg > 7) ? &sh_reg(c,a[reg&7]) : &sh_reg(c,d[reg]);
            if (op & (1u<<10)) {
                bail_if(rc = read_ea(c));
                *r = c->op_sz == OPSZ_L
                    ? c->p->operand.val
                    : (*r & ~0xffffu) | (uint16_t)c->p->operand.val;
            } else {
                c->p->operand.val = *r;
                bail_if(rc = write_ea(c));
            }
            if (predec)
                c->p->operand.mem -= c->op_sz == OPSZ_W ? 2 : 4;
            else
                c->p->operand.mem += c->op_sz == OPSZ_W ? 2 : 4;
        }
        if (predec)
            c->p->operand.mem += c->op_sz == OPSZ_W ? 2 : 4;
        if (predec || ((op & 0x38u) == 0x18u)) /* predec / postinc */
            *c->p->operand.reg = c->p->operand.mem;
    } else if ((op & 0xffc0u) == 0x4c00u) {
        /* muls/mulu.l */
        uint16_t ext, dh, dl, sz;
        bail_if(rc = fetch_insn_word(c, &ext));
        dh = ext&7; dl = (ext>>12)&7; sz = (ext>>10)&1;
        c->op_sz = OPSZ_L;
        dump(c, "mul%c.l\t", ext & (1u<<11) ? 's' : 'u');
        bail_if(rc = decode_ea(c));
        dump(c, ",");
        if (sz)
            dump(c, "%s:", dreg[dh]);
        dump(c, "%s", dreg[dl]);
        rc = M68KEMUL_UNHANDLEABLE;
    } else if ((op & 0xffc0u) == 0x4800u) {
        /* nbcd */
        c->op_sz = OPSZ_B;
        dump(c, "nbcd.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        rc = M68KEMUL_UNHANDLEABLE;
    } else if (((op & 0xff00u) == 0x4400u) &&
               ((c->op_sz = (op>>6)&3) != OPSZ_X)) {
        /* neg */
        uint32_t s;
        dump(c, "neg.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        bail_if(rc = read_ea(c));
        s = c->p->operand.val;
        c->p->operand.val = 0;
        rc = op_sub(c, s);
    } else if (((op & 0xff00u) == 0x4000u) &&
               ((c->op_sz = (op>>6)&3) != OPSZ_X)) {
        /* negx */
        uint32_t s;
        uint16_t sr;
        dump(c, "negx.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        bail_if(rc = read_ea(c));
        s = c->p->operand.val;
        c->p->operand.val = 0;
        sr = sh_reg(c, sr);
        bail_if(rc = op_sub(c, s));
        if (sr & CC_X) {
            uint16_t sr2 = sh_reg(c, sr);
            bail_if(rc = op_sub(c, 1));
            /* overflow and carry accumulate across the two subtracts */
            sh_reg(c, sr) |= sr2 & (CC_X|CC_V|CC_C);
        }
        /* CC.Z is never set by this instruction, only cleared */
        if ((sh_reg(c, sr) & CC_Z) && !(sr & CC_Z))
            sh_reg(c, sr) &= ~CC_Z;
    } else if (((op & 0xff00u) == 0x4600u) &&
               ((c->op_sz = (op>>6)&3) != OPSZ_X)) {
        /* not */
        dump(c, "not.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        bail_if(rc = read_ea(c));
        c->p->operand.val = ~c->p->operand.val;
        cc_mov(c, c->p->operand.val);
        rc = write_ea(c);
    } else if ((op & 0xffc0u) == 0x4840u) {
        /* pea */
        c->op_sz = OPSZ_L;
        dump(c, "pea.l\t");
        bail_if(rc = decode_mem_ea(c));
        sh_reg(c, a[7]) -= 4;
        bail_if(rc = write(sh_reg(c, a[7]), c->p->operand.mem, 4, c));
    } else if ((op & 0xffc0u) == 0x4ac0u) {
        /* tas */
        c->op_sz = OPSZ_B;
        dump(c, "tas.b\t");
        bail_if(rc = decode_ea(c));
        bail_if(rc = read_ea(c));
        sh_reg(c, sr) &= ~(CC_N|CC_Z|CC_V|CC_C);
        if (c->p->operand.val & 0x80)
            sh_reg(c, sr) |= CC_N;
        if (!(c->p->operand.val & 0xff))
            sh_reg(c, sr) |= CC_Z;
        c->p->operand.val |= 0x80;
        bail_if(rc = write_ea(c));
    } else if ((op & 0xfff0u) == 0x4e40u) {
        /* trap */
        uint8_t trap = op & 15;
        dump(c, "trap\t#%x", trap);
        raise_exception(M68KVEC_trap_0 + trap);
    } else if (((op & 0xff00u) == 0x4a00u) &&
               ((c->op_sz = (op>>6)&3) != OPSZ_X)) {
        /* tst */
        dump(c, "tst.%c\t", op_sz_ch[c->op_sz]);
        bail_if(rc = decode_ea(c));
        bail_if(rc = read_ea(c));
        cc_mov(c, c->p->operand.val);
    } else {
    unknown:
        dump(c, "???");
        raise_exception(M68KVEC_illegal_insn);
    }

bail:
    return rc;
}

int m68k_emulate(struct m68k_emulate_ctxt *c)
{
    struct m68k_emulate_priv_ctxt priv = {
        .sh_regs = *c->regs,
        .dis_p = c->dis
    };
    uint16_t op;
    int rc, trace = !!(c->regs->sr & SR_T);

    /* Initialise emulator state. */
    c->p = &priv;
    c->dis[0] = '\0';
    c->op_sz = OPSZ_X;
    c->op_words = 0;
    c->cycles = 0;
    bail_if(rc = fetch_insn_word(c, &op));

    switch ((op >> 12) & 0xf) {
    case 0x0: { /* COMPLETE (but callm/cas/cas2/chk2/cmp2/moves/rtm) */
        static const char *imm_alu_op[] = {
            "or", "and", "sub", "add", NULL, "eor", "cmp", NULL };
        if (!(op & 0x0100u) && imm_alu_op[(op>>9)&7]) {
            /* addi/andi/cmpi/eori/ori/subi */
            uint32_t imm;
            c->op_sz = (op >> 6) & 3;
            bail_if(rc = fetch_insn_ubytes(c, &imm, c->op_sz));
            dump(c, "%si.%c\t", imm_alu_op[(op>>9)&7],
                 op_sz_ch[c->op_sz]);
            dump(c, "#%x,", imm);
            if ((op & 0x3fu) == 0x3cu) {
                dump(c, "%s", (c->op_sz==OPSZ_B) ? "ccr" : "sr");
                c->p->operand.type = OP_SR;
                raise_exception_if(
                    (c->op_sz != OPSZ_B) && !(sh_reg(c, sr) & SR_S),
                    M68KVEC_priv_violation);
            } else {
                bail_if(rc = decode_ea(c));
            }

            bail_if(rc = read_ea(c));
            switch ((op >> 9) & 7) {
            case 0: /* or */
                c->p->operand.val |= imm;
                cc_mov(c, c->p->operand.val);
                bail_if(rc = write_ea(c));
                break;
            case 1: /* and */
                c->p->operand.val &= imm;
                cc_mov(c, c->p->operand.val);
                bail_if(rc = write_ea(c));
                break;
            case 2: /* sub */
                bail_if(rc = op_sub(c, imm));
                break;
            case 3: /* add */
                bail_if(rc = op_add(c, imm));
                break;
            case 5: /* eor */
                c->p->operand.val ^= imm;
                cc_mov(c, c->p->operand.val);
                bail_if(rc = write_ea(c));
                break;
            case 6: /* cmp */
                op_cmp(c, imm, c->p->operand.val);
                break;
            default:
                rc = M68KEMUL_UNHANDLEABLE;
                break;
            }
        } else if ((op & 0xf138u) == 0x0108u) {
            /* movep */
            uint32_t v=0, b, *reg = &sh_reg(c, d[(op>>9)&7]);
            int i;
            c->op_sz = op & (1u<<6) ? OPSZ_L : OPSZ_W;
            dump(c, "movep.%c\t", op_sz_ch[c->op_sz]);
            if (op & (1u<<7))
                dump(c, "%s,", dreg[(op>>9)&7]);
            c->op[0] |= 1u<<5; /* fix the ea mode to be d16(An) */
            bail_if(rc = decode_ea(c));
            c->op[0] = op;
            if (!(op & (1u<<7))) {
                dump(c, ",%s", dreg[(op>>9)&7]);
                for (i = 0; i < (c->op_sz == OPSZ_L ? 4 : 2); i++) {
                    bail_if(rc = read(c->p->operand.mem+i*2, &b, 1, c));
                    v = (v << 8) | b;
                }
                *reg = c->op_sz == OPSZ_W
                    ? (*reg & ~0xffffu) | (uint16_t)v : v;
            } else {
                v = *reg;
                for (i = (c->op_sz == OPSZ_L ? 3 : 1); i >= 0; i--) {
                    bail_if(rc = write(c->p->operand.mem+i*2, v, 1, c));
                    v >>= 8;
                }
            }
        } else {
            /* bchg/bclr/bset/btst */
            static const char *bitop[] = {
                "btst", "bchg", "bclr", "bset" };
            uint16_t idx;
            c->op_sz = !(op & 0x38u) ? OPSZ_L: OPSZ_B;
            dump(c, "%s.%c\t", bitop[(op>>6)&3], op_sz_ch[c->op_sz]);
            if (op & (1u<<8)) {
                idx = sh_reg(c, d[(op>>9)&7]);
                dump(c, "%s,", dreg[(op>>9)&7]);
            } else if ((op & 0x0f00u) == 0x0800u) {
                bail_if(rc = fetch_insn_word(c, &idx));
                idx &= c->op_sz == OPSZ_B ? 7 : 31;
                dump(c, "#%x,", idx);
            } else {
                goto unknown;
            }
            bail_if(rc = decode_ea(c));
            bail_if(rc = read_ea(c));
            idx &= c->op_sz == OPSZ_B ? 7 : 31;
            sh_reg(c, sr) &= ~CC_Z;
            if (!(c->p->operand.val & (1u<<idx)))
                sh_reg(c, sr) |= CC_Z;
            switch ((op >> 6 ) & 3) {
            case 1: c->p->operand.val ^= 1u << idx; break;
            case 2: c->p->operand.val &= ~(1u << idx); break;
            case 3: c->p->operand.val |= 1u << idx; break;
            }
            bail_if(((op >> 6) & 3) && (rc = write_ea(c)));
        }
        break;
    }
    case 0x1: /* COMPLETE */
        c->op_sz = OPSZ_B;
        goto move;
    case 0x2: /* COMPLETE */
        c->op_sz = OPSZ_L;
        goto move;
    case 0x3: { /* COMPLETE */
        struct operand src, dst;
        c->op_sz = OPSZ_W;
    move:
        if (((op >> 6) & 7) == 1) {
            if (c->op_sz == OPSZ_B)
                goto unknown;
            dump(c, "movea.%c\t", op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", areg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            if (c->op_sz == OPSZ_W) {
                c->p->operand.val = (int16_t)c->p->operand.val;
                c->op_sz = OPSZ_L;
            }
            sh_reg(c, a[(op>>9)&7]) = c->p->operand.val;
        } else {
            dump(c, "move.%c\t", op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
            src = c->p->operand;
            dump(c, ",");
            /* Swizzle the opcode to shift dst ea to the right place */
            c->op[0] = ((op >> 9) & 0x07) | ((op >> 3) & 0x38);
            bail_if(rc = decode_ea(c));
            dst = c->p->operand;
            c->op[0] = op; /* restore */
            c->p->operand = src;
            bail_if(rc = read_ea(c));
            dst.val = c->p->operand.val;
            c->p->operand = dst;
            bail_if(rc = write_ea(c));
            cc_mov(c, dst.val);
        }
        /*
         * Most move instructions perform the second prefetch after writeback.
         * We simulate this by discarding our second word of prefetch.
         */
        if (c->prefetch_valid > 1)
            c->prefetch_valid = 1;
        break;
    }
    case 0x4: { /* COMPLETE */
        rc = misc_insn(c);
        break;
    }
    case 0x5: { /* COMPLETE */
        static const char *cc[] = {
            "t",  "f",  "hi", "ls", "cc", "cs", "ne", "eq",
            "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le" };
        uint8_t cond = (op >> 8) & 0xf;
        if ((op & 0x00c0u) != 0x00c0u) {
            /* addq/subq */
            uint8_t val = (op >> 9) & 7 ? : 8;
            c->op_sz = (op >> 6) & 3;
            dump(c, "%sq.%c\t#%x,",
                 op & (1u<<8) ? "sub" : "add",
                 op_sz_ch[c->op_sz], val);
            bail_if(rc = decode_ea(c));
            bail_if(rc = read_ea(c));
            if (((op >> 3) & 7) == 1) {
                /* adda/suba semantics */
                uint32_t *reg = c->p->operand.reg;
                c->op_sz = OPSZ_L;
                *reg = op & (1u<<8) ? *reg - val : *reg + val;
            } else {
                bail_if(rc = ((op & (1u<<8)) ? op_sub : op_add)(c, val));
            }
        } else if ((op & 0x0038u) == 0x0008u) {
            /* dbcc */
            uint32_t pc = sh_reg(c,pc);
            int32_t disp;
            bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_W));
            dump(c, "db%s.w\t%s,%04x", cc[cond], dreg[op&7], pc + disp);
            if (!cc_eval_condition(c, cond)) {
                uint32_t *reg = &sh_reg(c, d[op&7]);
                *reg = (*reg & ~0xffffu) | (uint16_t)(*reg - 1);
                if ((int16_t)*reg != -1)
                    sh_reg(c, pc) = pc + disp;
            }
        } else if ((op & 0x003fu) >= 0x003au) {
            /* trapcc */
            uint32_t imm;
            dump(c, "trap%s", cc[cond]);
            if (op & 2) {
                c->op_sz = op&1 ? OPSZ_L : OPSZ_W;
                bail_if(rc = fetch_insn_ubytes(c, &imm, c->op_sz));
                dump(c, "\t#%x", imm);
            }
            raise_exception_if(cc_eval_condition(c, cond),
                               M68KVEC_trapcc_trapv);
        } else {
            /* scc */
            c->op_sz = OPSZ_B;
            dump(c, "s%s.b\t", cc[cond]);
            bail_if(rc = decode_ea(c));
            c->p->operand.val = cc_eval_condition(c, cond) ? ~0 : 0;
            bail_if(rc = write_ea(c));
        }
        break;
    }
    case 0x6: { /* COMPLETE */
        static const char *cc[] = {
            "ra", "sr", "hi", "ls", "cc", "cs", "ne", "eq",
            "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le" };
        uint32_t target = sh_reg(c,pc);
        int32_t disp = (int8_t)op;
        uint8_t cond = (op >> 8) & 0xf;
        c->op_sz = (disp == 0  ? OPSZ_W : disp == -1 ? OPSZ_L : OPSZ_B);
        dump(c, "b%s.%c", cc[cond], op_sz_ch[c->op_sz]);
        if (disp == 0)
            bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_W));
        else if (disp == -1)
            bail_if(rc = fetch_insn_sbytes(c, &disp, OPSZ_L));
        dump(c, "\t%04x", target + disp);
        if (cond == 1) {
            /* bsr: push return address (current pc) onto stack */
            sh_reg(c, a[7]) -= 4;
            bail_if(rc = write(sh_reg(c, a[7]), sh_reg(c, pc), 4, c));
        } else if (!cc_eval_condition(c, cond))
            break; /* bcc condition is false: no branch */
        sh_reg(c, pc) = target + disp;
        break;
    }
    case 0x7: { /* COMPLETE */
        uint32_t *reg = &sh_reg(c, d[(op>>9)&7]);
        int8_t val = (int8_t)op;
        *reg = val;
        c->op_sz = OPSZ_L;
        dump(c, "moveq\t#");
        if (val < 0) {
            dump(c, "-");
            val = -val;
        }
        dump(c, "%x,%s", val, dreg[(op>>9)&7]);
        cc_mov(c, *reg);
        break;
    }
    case 0x8: /* COMPLETE */
        goto case_0xc;
    case 0x9: /* COMPLETE */
        dump(c, "sub");
        goto addsub;
    case 0xa: /* COMPLETE */
        dump(c, "a-line");
        raise_exception(M68KVEC_a_line);
        break;
    case 0xb: { /* COMPLETE */
        c->op_sz = (op>>6)&3;
        if ((op & 0xc0u) == 0xc0u) {
            /* cmpa */
            c->op_sz = op & (1u<<8) ? OPSZ_L : OPSZ_W;
            dump(c, "cmpa.%c\t", op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", areg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            if (c->op_sz == OPSZ_W) {
                c->p->operand.val = (int16_t)c->p->operand.val;
                c->op_sz = OPSZ_L;
            }
            op_cmp(c, c->p->operand.val, sh_reg(c, a[(op>>9)&7]));
        } else if ((op & 0xf100u) == 0xb000u) {
            /* cmp */
            dump(c, "cmp.%c\t", op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", dreg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            op_cmp(c, c->p->operand.val, sh_reg(c, d[(op>>9)&7]));
        } else if ((op & 0xf138u) == 0xb108u) {
            /* cmpm */
            dump(c, "cmpm.%c\t(%s)+,(%s)+", op_sz_ch[c->op_sz],
                 areg[op&7], areg[(op>>9)&7]);
            rc = M68KEMUL_UNHANDLEABLE;
        } else {
            /* eor */
            dump(c, "eor.%c\t%s,", op_sz_ch[c->op_sz],
                           dreg[(op>>9)&7]);
            bail_if(rc = decode_ea(c));
            bail_if(rc = read_ea(c));
            c->p->operand.val ^= sh_reg(c, d[(op>>9)&7]);
            cc_mov(c, c->p->operand.val);
            bail_if(rc = write_ea(c));
        }
        break;
    }
    case 0xc: case_0xc: { /* COMPLETE */
        if ((op & 0xb1f0u) == 0x8100u) {
            /* abcd/sbcd */
            dump(c, "%cbcd.b\t", op & (1u<<14) ? 'a' : 's');
            if (op & (1u<<3))
                dump(c, "-(%s),-(%s)", areg[op&7], areg[(op>>9)&7]);
            else
                dump(c, "-%s,%s", dreg[op&7], dreg[(op>>9)&7]);
            rc = M68KEMUL_UNHANDLEABLE;
        } else if ((op & 0xf0c0u) == 0x80c0u) {
            /* divs.w/divu.w */
            uint32_t q, r, *reg = &sh_reg(c, d[(op>>9)&7]);
            c->op_sz = OPSZ_W;
            dump(c, "div%c.w\t", op & (1u<<8) ? 's' : 'u');
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", dreg[(op>>9)&7]);
            sh_reg(c, sr) &= ~(CC_N|CC_Z|CC_V|CC_C);
            bail_if(rc = read_ea(c));
            raise_exception_if((uint16_t)c->p->operand.val == 0,
                               M68KVEC_zero_divide);
            if (op & (1u<<8)) {
                q = (int32_t)*reg / (int16_t)c->p->operand.val;
                r = (int32_t)*reg % (int16_t)c->p->operand.val;
                if (((int32_t)q > 0x7fff) || ((int32_t)q < -0x8000))
                    sh_reg(c, sr) |= CC_V;
            } else {
                q = (uint32_t)*reg / (uint16_t)c->p->operand.val;
                r = (uint32_t)*reg % (uint16_t)c->p->operand.val;
                if (q > 0xffff)
                    sh_reg(c, sr) |= CC_V;
            }
            if (!(sh_reg(c, sr) & CC_V))
                *reg = (r << 16) | (uint16_t)q;
            if ((uint16_t)q == 0)
                sh_reg(c, sr) |= CC_Z;
            if ((int16_t)q < 0)
                sh_reg(c, sr) |= CC_N;
        } else if ((op & 0xf0c0u) == 0xc0c0u) {
            /* muls.w/mulu.w */
            uint32_t *reg = &sh_reg(c, d[(op>>9)&7]);
            c->op_sz = OPSZ_W;
            dump(c, "mul%c.w\t", op & (1u<<8) ? 's' : 'u');
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", dreg[(op>>9)&7]);
            sh_reg(c, sr) &= ~(CC_N|CC_Z|CC_V|CC_C);
            bail_if(rc = read_ea(c));
            if (op & (1u<<8))
                *reg = (int16_t)*reg * (int16_t)c->p->operand.val;
            else
                *reg = (uint16_t)*reg * (uint16_t)c->p->operand.val;
            if ((uint32_t)*reg == 0)
                sh_reg(c, sr) |= CC_Z;
            if ((int32_t)*reg < 0)
                sh_reg(c, sr) |= CC_N;
        } else if ((op & 0xf130u) == 0xc100u) {
            /* exg */
            uint32_t *r1, *r2, t;
            r1 = ((op & 0xf8u) == 0x48u ? sh_reg(c,a) : sh_reg(c,d));
            r2 = ((op & 0xf8u) == 0x40u ? sh_reg(c,d) : sh_reg(c,a));
            r1 += (op >> 9) & 7;
            r2 += op & 7;
            dump(c, "exg.l\t%s,%s",
                 ((op & 0xf8u) == 0x48u ? areg : dreg)[(op>>9)&7],
                 ((op & 0xf8u) == 0x40u ? dreg : areg)[op&7]);
            t = *r1;
            *r1 = *r2;
            *r2 = t;
        } else {
            /* and/or */
            uint32_t r, *reg = &sh_reg(c, d[(op>>9)&7]);
            c->op_sz = (op>>6) & 3;
            dump(c, "%s.%c\t",
                 op & (1u<<14) ? "and" : "or",
                 op_sz_ch[c->op_sz]);
            if (op & (1u<<8))
                dump(c, "%s,", dreg[(op>>9)&7]);
            bail_if(rc = decode_ea(c));
            if (!(op & (1u<<8)))
                dump(c, ",%s", dreg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            r = op & (1u<<14) ?
                c->p->operand.val & *reg : c->p->operand.val | *reg;
            cc_mov(c, r);
            if (!(op & (1u<<8))) {
                c->p->operand.type = OP_REG;
                c->p->operand.reg = reg;
            }
            c->p->operand.val = r;
            bail_if(rc = write_ea(c));
        }
        break;
    }
    case 0xd: { /* COMPLETE */
        dump(c, "add");
        addsub:
        c->op_sz = (op>>6)&3;
        if ((op & 0xc0u) == 0xc0u) {
            /* adda/suba */
            uint32_t r, *reg = &sh_reg(c, a[(op>>9)&7]);
            c->op_sz = op & (1u<<8) ? OPSZ_L : OPSZ_W;
            dump(c, "a.%c\t", op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
            dump(c, ",%s", areg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            r = c->p->operand.val;
            if (c->op_sz == OPSZ_W) {
                r = (int16_t)r;
                c->op_sz = OPSZ_L;
            }
            *reg = op & (1u<<14) ? *reg + r : *reg - r;
        } else if ((op & 0x130u) == 0x100u) {
            /* addx/subx */
            uint32_t op1;
            uint16_t sr;
            dump(c, "x.%c\t", op_sz_ch[c->op_sz]);
            if (op & (1u<<3)) {
                dump(c, "-(%s),-(%s)", areg[op&7], areg[(op>>9)&7]);
                c->p->operand.reg = &sh_reg(c, a[op&7]);
                c->p->operand.mem = *c->p->operand.reg -=
                    (c->op_sz == OPSZ_B ? 1 : c->op_sz == OPSZ_W ? 2 : 4);
                bail_if(rc = read_ea(c));
                op1 = c->p->operand.val;
                c->p->operand.reg = &sh_reg(c, a[(op>>9)&7]);
                c->p->operand.mem = *c->p->operand.reg -=
                    (c->op_sz == OPSZ_B ? 1 : c->op_sz == OPSZ_W ? 2 : 4);
                bail_if(rc = read_ea(c));
            } else {
                dump(c, "%s,%s", dreg[op&7], dreg[(op>>9)&7]);
                op1 = sh_reg(c, d[op&7]);
                c->p->operand.type = OP_REG;
                c->p->operand.reg = &sh_reg(c, d[(op>>9)&7]);
                c->p->operand.val = *c->p->operand.reg;
            }
            sr = sh_reg(c, sr);
            bail_if(rc = ((op & (1u<<14)) ? op_add : op_sub)(c, op1));
            if (sr & CC_X) {
                uint16_t sr2 = sh_reg(c, sr);
                bail_if(rc = ((op & (1u<<14)) ? op_add : op_sub)(c, 1));
                /* overflow and carry accumulate */
                sh_reg(c, sr) |= sr2 & (CC_X|CC_V|CC_C);
            }
            /* CC.Z is never set by this instruction, only cleared */
            if ((sh_reg(c, sr) & CC_Z) && !(sr & CC_Z))
                sh_reg(c, sr) &= ~CC_Z;
        } else {
            /* add/sub */
            uint32_t op1, *reg = &sh_reg(c, d[(op>>9)&7]);
            op1 = *reg;
            dump(c, ".%c\t", op_sz_ch[c->op_sz]);
            if (op & (1u<<8))
                dump(c, "%s,", dreg[(op>>9)&7]);
            bail_if(rc = decode_ea(c));
            if (!(op & (1u<<8)))
                dump(c, ",%s", dreg[(op>>9)&7]);
            bail_if(rc = read_ea(c));
            if (!(op & (1u<<8))) {
                op1 = c->p->operand.val;
                c->p->operand.type = OP_REG;
                c->p->operand.reg = reg;
                c->p->operand.val = *reg;
            }
            bail_if(rc = ((op & (1u<<14)) ? op_add : op_sub)(c, op1));
        }
        break;
    }
    case 0xe: { /* COMPLETE */
        static const char *sr[] = {
            "as", "ls", "rox", "ro" };
        uint32_t m, v;
        uint8_t x, typ, cnt;
        if ((op & 0xf8c0u) == 0xe8c0u) {
            /* bitfield access */
            goto unknown;
        } else if ((op & 0xc0u) == 0xc0u) {
            /* shift/rotate <ea> */
            c->op_sz = OPSZ_W;
            typ = (op >> 9) & 3;
            cnt = 1;
            dump(c, "%s%c.%c\t", sr[typ],
                 op&(1u<<8) ? 'l' : 'r', op_sz_ch[c->op_sz]);
            bail_if(rc = decode_ea(c));
        } else {
            /* shift/rotate <dn> */
            c->op_sz = (op >> 6) & 3;
            typ = (op >> 3) & 3;
            dump(c, "%s%c.%c\t", sr[typ],
                 op&(1u<<8) ? 'l' : 'r', op_sz_ch[c->op_sz]);
            if (op & (1u<<5)) {
                cnt = sh_reg(c, d[(op>>9)&7]) & 63;
                dump(c, "%s", dreg[(op>>9)&7]);
            } else {
                cnt = (op >> 9) & 7 ?: 8;
                dump(c, "#%x", cnt);
            }
            dump(c, ",%s", dreg[op&7]);
            c->p->operand.type = OP_REG;
            c->p->operand.reg = &sh_reg(c, d[op&7]);
        }
        bail_if(rc = read_ea(c));
        v = c->p->operand.val;
        m = 1u << (c->op_sz == OPSZ_L ? 31 : c->op_sz == OPSZ_W ? 15 : 7);
        sh_reg(c, sr) &= ~(CC_N|CC_Z|CC_V|CC_C);
        while (cnt--) {
            switch ((typ << 1) | ((op >> 8) & 1)) {
            case 0: /* asr */
                sh_reg(c, sr) &= ~(CC_X|CC_C);
                if (v & 1)
                    sh_reg(c, sr) |= CC_X|CC_C;
                v = (v >> 1) | (v & m);
                break;
            case 1: /* asl */
                sh_reg(c, sr) &= ~(CC_X|CC_C);
                if (v & m)
                    sh_reg(c, sr) |= CC_X|CC_C;
                if ((v ^ (v << 1)) & m)
                    sh_reg(c, sr) |= CC_V;
                v = (v << 1);
                break;
            case 2: /* lsr */
                sh_reg(c, sr) &= ~(CC_X|CC_C);
                if (v & 1)
                    sh_reg(c, sr) |= CC_X|CC_C;
                v = (v >> 1);
                break;
            case 3: /* lsl */
                sh_reg(c, sr) &= ~(CC_X|CC_C);
                if (v & m)
                    sh_reg(c, sr) |= CC_X|CC_C;
                v = (v << 1);
                break;
            case 4: /* roxr */
                x = !!(v & 1);
                v = (v >> 1) | (sh_reg(c, sr) & CC_X ? m : 0);
                sh_reg(c, sr) &= ~CC_X;
                sh_reg(c, sr) |= x ? CC_X : 0;
                break;
            case 5: /* roxl */
                x = !!(v & m);
                v = (v << 1) | (sh_reg(c, sr) & CC_X ? 1 : 0);
                sh_reg(c, sr) &= ~CC_X;
                sh_reg(c, sr) |= x ? CC_X : 0;
                break;
            case 6: /* ror */
                sh_reg(c, sr) &= ~CC_C;
                if (v & 1)
                    sh_reg(c, sr) |= CC_C;
                v = (v >> 1) | (sh_reg(c, sr) & CC_C ? m : 0);
                break;
            case 7: /* rol */
                sh_reg(c, sr) &= ~CC_C;
                if (v & m)
                    sh_reg(c, sr) |= CC_C;
                v = (v << 1) | (sh_reg(c, sr) & CC_C ? 1 : 0);
                break;
            }
        }
        if (typ == 2) /* roxl/roxr */
            sh_reg(c, sr) |= sh_reg(c, sr) & CC_X ? CC_C : 0;
        v &= (m << 1) - 1;
        sh_reg(c, sr) |= (v == 0 ? CC_Z : 0) | (v & m ? CC_N : 0);
        c->p->operand.val = v;
        rc = write_ea(c);
        break;
    }
    case 0xf: /* COMPLETE */
        dump(c, "f-line");
        raise_exception(M68KVEC_f_line);
        break;
    default: unknown:
        dump(c, "???");
        raise_exception(M68KVEC_illegal_insn);
        break;
    }

bail:
    if (!c->emulate || (rc == M68KEMUL_UNHANDLEABLE))
        goto out;

    /* Check for unaligned instruction prefetch. */
    rc = check_addr_align(c, sh_reg(c, pc), 2, access_fetch) ? : rc;

    /* Exception/trap processing. */
    if ((rc != M68KEMUL_EXCEPTION) ||
        (c->p->exception.vector == M68KVEC_zero_divide) ||
        (c->p->exception.vector == M68KVEC_chk_chk2) ||
        (c->p->exception.vector == M68KVEC_trapcc_trapv) ||
        (c->p->exception.vector >= M68KVEC_trap_0)) {
        /* No instruction-aborting exception? Write back register state. */
        *c->regs = c->p->sh_regs;
    } else {
        /* Instruction was aborted. Discard register state; no trace. */
        trace = 0;

        /*
         * Address/bus errors have PC "in the vicinity of" the instruction.
         * Just past the main opcode word seems to work well.
         */
        if ((c->p->exception.vector == M68KVEC_addr_error) ||
            (c->p->exception.vector == M68KVEC_bus_error))
            c->regs->pc += 2;
    }

    /* Now deliver exceptions and traps. */
    if (rc == M68KEMUL_EXCEPTION)
        rc = deliver_exception(c);

    /* Finally, check trace mode. */
    if (trace && (rc != M68KEMUL_UNHANDLEABLE)) {
        c->p->exception.vector = M68KVEC_trace;
        rc = deliver_exception(c);
    }

out:
    if (rc != M68KEMUL_UNHANDLEABLE)
        rc = M68KEMUL_OKAY;
    return rc;
}

void m68k_dump_regs(struct m68k_regs *r, void (*print)(const char *, ...))
{
    print("D0: %08x D1: %08x D2: %08x D3: %08x\n",
          r->d[0], r->d[1], r->d[2], r->d[3]);
    print("D4: %08x D5: %08x D6: %08x D7: %08x\n",
          r->d[4], r->d[5], r->d[6], r->d[7]);
    print("A0: %08x A1: %08x A2: %08x A3: %08x\n",
          r->a[0], r->a[1], r->a[2], r->a[3]);
    print("A4: %08x A5: %08x A6: %08x A7: %08x\n",
          r->a[4], r->a[5], r->a[6], r->a[7]);
    print("PC: %08x SR: %04x USP: %08x SSP: %08x\n",
          r->pc, r->sr, (r->sr & SR_S) ? r->xsp : r->a[7],
          (r->sr & SR_S) ? r->a[7] : r->xsp);
    print("S=%u T=%u I=%u X=%u N=%u Z=%u V=%u C=%u\n",
          !!(r->sr & SR_S), !!(r->sr & SR_T), (r->sr >> 8) & 7,
          !!(r->sr & CC_X), !!(r->sr & CC_N), !!(r->sr & CC_Z),
          !!(r->sr & CC_V), !!(r->sr & CC_C));
}

void m68k_dump_stack(
    struct m68k_emulate_ctxt *c, enum stack stack,
    void (*print)(const char *, ...))
{
    int i;
    uint32_t sp = c->regs->a[7];
    if (((stack == stack_user) && (c->regs->sr & SR_S)) ||
        ((stack == stack_super) && !(c->regs->sr & SR_S)))
        sp = c->regs->xsp;
    print("Stack trace from %s=%08x:\n",
          stack == stack_user ? "USP" : stack == stack_super ? "SSP" : "SP",
          sp);
    for (i = 0 ; i < 24; i++) {
        uint32_t v;
        if ((i & 7) == 0)
            print(" %08x: ", sp);
        if (c->ops->read(sp, &v, 2, c))
            print("???? ");
        else
            print("%04x ", v);
        if ((i & 7) == 7)
            print("\n");
        sp += 2;
    }
}

int m68k_deliver_exception(
    struct m68k_emulate_ctxt *c, struct m68k_exception *e)
{
    uint16_t old_sr = c->regs->sr;
    uint32_t old_pc = c->regs->pc;
    int rc;

    c->p->sh_regs = *c->regs;

    update_sr(c, (old_sr | SR_S) & ~SR_T);
    bail_if(rc = read(4*e->vector, &sh_reg(c, pc), 4, c));

    sh_reg(c, a[7]) -= 6;
    bail_if(rc = write(sh_reg(c, a[7]) + 2, old_pc, 4, c));
    bail_if(rc = write(sh_reg(c, a[7]) + 0, old_sr, 2, c));

    /* Bus error or address error? */
    if (e->vector <= M68KVEC_addr_error) {
        /* Extended stack frame with faulting access information. */
        sh_reg(c, a[7]) -= 8;
        bail_if(rc = write(sh_reg(c, a[7]) + 0, e->status_word, 2, c));
        bail_if(rc = write(sh_reg(c, a[7]) + 2, e->fault_addr, 4, c));
        bail_if(rc = write(sh_reg(c, a[7]) + 6, c->op[0], 2, c));
    }

    *c->regs = c->p->sh_regs;

bail:
    return rc;
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
