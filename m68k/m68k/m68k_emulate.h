/*
 * m68k_emulate.h
 * 
 * Emulate/disassemble m680x0 opcodes.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __M68K_EMULATE_H__
#define __M68K_EMULATE_H__

struct m68k_emulate_ctxt;
struct m68k_exception;

/* Return codes from state-accessor functions and from m68k_emulate(). */
 /* Completed successfully. State modified appropriately. */
#define M68KEMUL_OKAY           0
 /* Unhandleable access or emulation. No state modified. */
#define M68KEMUL_UNHANDLEABLE   1
 /* Exception raised and requires delivery. */
#define M68KEMUL_EXCEPTION      2

/* These operations represent the instruction emulator's interface to memory,
 * privileged state... pretty much everything other than GPRs. */
struct m68k_emulate_ops
{
    /* All functions:
     *  @ctxt:  [IN ] Emulation context info as passed to the emulator.
     *  @addr:  [IN ] Address in emulated environment.
     *  @bytes: [IN ] Number of bytes to read or write. Valid access sizes are
     *                1, 2, and 4. */

    /* read: Emulate a memory read.
     *  @val:   [OUT] Value read, zero-extended to 'uint32_t'.
     * NB. This callback is used only for insn fetch if ctxt.emulate = 0. */
    int (*read)(
        uint32_t addr,
        uint32_t *val,
        unsigned int bytes,
        struct m68k_emulate_ctxt *ctxt);

    /* write: Emulate a memory write.
     *  @val:   [IN ] Value to write (low-order bytes used as req'd).
     * NB. This callback is not used if ctxt.emulate = 0. */
    int (*write)(
        uint32_t addr,
        uint32_t val,
        unsigned int bytes,
        struct m68k_emulate_ctxt *ctxt);

    /* addr_name: Obtain a mnemonic name for EA <addr>, else NULL.
     * NB. This callback can be left undefined as NULL.
     *     This callback is not used if ctxt.disassemble = 0. */
    const char *(*addr_name)(
        uint32_t addr,
        struct m68k_emulate_ctxt *ctxt);

    /* deliver_exception: Deliver /interrupt/trap/exception <vector>.
     * NB. This callback may use m68k_deliver_exception() if appropriate.
     *     m68k_emulate() will do so automatically if this callback is NULL. */
    int (*deliver_exception)(
        struct m68k_emulate_ctxt *ctxt,
        struct m68k_exception *exception);
};

/* Just the basic user-visible (except sr[15:8]) set. */
struct m68k_regs {
    uint32_t d[8], a[8], pc;
    uint32_t xsp; /* inactive SP (SSP if SR.S=0; USP if SR.S=1) */
    uint16_t sr;
};

/* m68k_emulate_ctxt.op_sz */
#define OPSZ_B 0 /* byte/1 */
#define OPSZ_W 1 /* word/2 */
#define OPSZ_L 2 /* long/4 */
#define OPSZ_X 3 /* none/unknown */

struct m68k_emulate_priv_ctxt;

struct m68k_emulate_ctxt
{
    /* IN: Pointer to register state before/after emulation. */
    struct m68k_regs *regs;

    /* IN: Pointer to state accessor callbacks. */
    const struct m68k_emulate_ops *ops;

    uint8_t disassemble:1;
    uint8_t emulate:1;

    /* OUT: Disassembly of the emulated instruction. */
    char dis[128];

    /* OUT: Decoded operation size. */
    uint8_t op_sz;

    /* OUT: Opcode words, and # words. */
    uint8_t op_words;
    uint16_t op[8];

    /* OUT: Number of cycles to execute on an M68000. */
    uint16_t cycles;

    /* PRIVATE: Prefetch data. */
    uint32_t prefetch_addr, prefetch_valid;
    uint16_t prefetch_dat[2];

    /* PRIVATE */
    struct m68k_emulate_priv_ctxt *p;
};

/* m68k_emulate: Emulate an instruction.
 * Returns M68KEMUL_OKAY or M68KEMUL_UNHANDLEABLE. */
int m68k_emulate(struct m68k_emulate_ctxt *);

/* m68k_dump_regs: Print register dump to stdout. */
void m68k_dump_regs(struct m68k_regs *, void (*print)(const char *, ...));

/* m68k_dump_stack: Print stack context.
 *  @stack: Which stack context to dump (user,supervisor,current). */
enum stack { stack_current, stack_user, stack_super };
void m68k_dump_stack(
    struct m68k_emulate_ctxt *, enum stack,
    void (*print)(const char *, ...));

struct m68k_exception {
    uint8_t vector;
    /* M68KVEC_{addr,bus}_error only: */
    uint16_t status_word; /* top of 68000 extended stack frame */
    uint32_t fault_addr;  /* faulting access address */
};

/* m68k_dump_stack: Deliver the specified exception into emulated context. */
int m68k_deliver_exception(
    struct m68k_emulate_ctxt *, struct m68k_exception *);

/* Handy vector definitions. */
#define M68KVEC_bus_error      0x02
#define M68KVEC_addr_error     0x03
#define M68KVEC_illegal_insn   0x04
#define M68KVEC_zero_divide    0x05
#define M68KVEC_chk_chk2       0x06
#define M68KVEC_trapcc_trapv   0x07
#define M68KVEC_priv_violation 0x08
#define M68KVEC_trace          0x09
#define M68KVEC_a_line         0x0a
#define M68KVEC_f_line         0x0b
#define M68KVEC_trap_0         0x20

#endif /* __M68K_EMULATE_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
