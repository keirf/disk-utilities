/*
 * cia.h
 * 
 * Emulate Amiga 8520 CIA chips.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __CIA_H__
#define __CIA_H__

#include <amiga/event.h>

#define CIA_TICK_NS (M68K_CYCLE_NS*10)

struct cia {
    /* Peripheral data registers, and their direction masks. */
    uint8_t pra_o, pra_i, prb_o, prb_i, ddra, ddrb;
    /* Timer A/B latches, and when the timer started in current mode. */
    uint16_t ta_latch, tb_latch;
    time_ns_t ta_started, tb_started;
    /* TOD latch, and when TOD started counting up from this value. */
    uint32_t tod_latch;
    time_ns_t tod_started;
    /* ICR */
    uint8_t icrr, icrw;
    /* Control registers */
    uint8_t cra, crb;
};

struct amiga_state;

void cia_write_reg(
    struct amiga_state *, struct cia *, uint8_t off, uint8_t val);
uint8_t cia_read_reg(
    struct amiga_state *, struct cia *, uint8_t off);
void cia_set_icr_flag(
    struct amiga_state *, struct cia *, uint8_t bit);

extern const char *cia_reg_name[16];

/* CIA registers. */
#define CIAPRA    0x0
#define CIAPRB    0x1
#define CIADDRA   0x2
#define CIADDRB   0x3
#define CIATALO   0x4
#define CIATAHI   0x5
#define CIATBLO   0x6
#define CIATBHI   0x7
#define CIATODLOW 0x8
#define CIATODMID 0x9
#define CIATODHI  0xa
#define CIASDR    0xc
#define CIAICR    0xd
#define CIACRA    0xe
#define CIACRB    0xf

/* interrupt control register bit numbers */
#define CIAICRB_TA      0
#define CIAICRB_TB      1
#define CIAICRB_ALRM    2
#define CIAICRB_SP      3
#define CIAICRB_FLG     4
#define CIAICRB_IR      7
#define CIAICRB_SETCLR  7

/* control register A bit numbers */
#define CIACRAB_START   0
#define CIACRAB_PBON    1
#define CIACRAB_OUTMODE 2
#define CIACRAB_RUNMODE 3
#define CIACRAB_LOAD    4
#define CIACRAB_INMODE  5
#define CIACRAB_SPMODE  6
#define CIACRAB_TODIN   7

/* control register B bit numbers */
#define CIACRBB_START   0
#define CIACRBB_PBON    1
#define CIACRBB_OUTMODE 2
#define CIACRBB_RUNMODE 3
#define CIACRBB_LOAD    4
#define CIACRBB_INMODE0 5
#define CIACRBB_INMODE1 6
#define CIACRBB_ALARM   7

/* ciaa port A (0xbfe001) */
#define CIAAPRA_GAMEPORT1  7   /* gameport 1, pin 6 (fire button*) */
#define CIAAPRA_GAMEPORT0  6   /* gameport 0, pin 6 (fire button*) */
#define CIAAPRA_DSKRDY     5   /* disk ready* */
#define CIAAPRA_DSKTRACK0  4   /* disk on track 00* */
#define CIAAPRA_DSKPROT    3   /* disk write protect* */
#define CIAAPRA_DSKCHANGE  2   /* disk change* */
#define CIAAPRA_LED        1   /* led light control (0==>bright) */
#define CIAAPRA_OVERLAY    0   /* memory overlay bit */

/* ciab port B (0xbfd100) -- disk control */
#define CIABPRB_DSKMOTOR   7   /* disk motor* */
#define CIABPRB_DSKSEL3    6   /* disk select unit 3* */
#define CIABPRB_DSKSEL2    5   /* disk select unit 2* */
#define CIABPRB_DSKSEL1    4   /* disk select unit 1* */
#define CIABPRB_DSKSEL0    3   /* disk select unit 0* */
#define CIABPRB_DSKSIDE    2   /* disk side select* */
#define CIABPRB_DSKDIREC   1   /* disk direction of seek* */
#define CIABPRB_DSKSTEP    0   /* disk step heads* */

#endif /* __CIA_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
