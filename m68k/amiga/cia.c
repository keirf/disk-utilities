/*
 * cia.c
 * 
 * Emulate Amiga 8520 CIA chips.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdio.h>
#include <amiga/amiga.h>

#define SUBSYSTEM subsystem_cia

#define is_ciaa(cia, s) ((cia) == &(s)->ciaa)
#define is_ciab(cia, s) ((cia) == &(s)->ciab)

static const char *cia_name(struct cia *cia, struct amiga_state *s)
{
    return is_ciaa(cia,s) ? "ciaa" : "ciab";
}

void cia_write_reg(
    struct amiga_state *s, struct cia *cia, uint8_t off, uint8_t val)
{
    log_info("%s.%s: write %02x", cia_name(cia,s), cia_reg_name[off], val);

    switch (off) {
    case CIAPRA:
        cia->pra_o = val;
        break;
    case CIAPRB:
        cia->prb_o = val;
        if (is_ciab(cia, s))
            disk_cia_changed(s);
        break;
    case CIADDRA:
        cia->ddra = val;
        break;
    case CIADDRB:
        cia->ddrb = val;
        break;
    case CIATALO:
        cia->ta_latch = (cia->ta_latch & 0xff00) | val;
        break;
    case CIATAHI:
        cia->ta_latch = (cia->ta_latch & 0xff) | ((uint16_t)val<<8);
        /* Load current time if CRA[0]=0 (time stopped) */
        /* Start timer if CRA[3]=1 (one-shot mode) */
        break;
    case CIATBLO:
        cia->tb_latch = (cia->tb_latch & 0xff00) | val;
        break;
    case CIATBHI:
        cia->tb_latch = (cia->tb_latch & 0xff) | ((uint16_t)val<<8);
        /* Load current time if CRB[0]=0 (time stopped) */
        /* Start timer if CRB[3]=1 (one-shot mode) */
        break;
    case CIAICR:
        if (val & 0x80)
            cia->icrw |= val & 0x7f;
        else
            cia->icrw &= ~val;
        break;
    case CIACRA:
        cia->cra = val;
        break;
    case CIACRB:
        cia->crb = val;
        break;
    default:
        log_error("Ignoring write to %s.%s\n",
                  cia_name(cia,s), cia_reg_name[off]);
        break;
    }
}

uint8_t cia_read_reg(
    struct amiga_state *s, struct cia *cia, uint8_t off)
{
    uint8_t val = 0xff;

    switch (off) {
    case CIAPRA:
        val = (cia->pra_i & ~cia->ddra) | (cia->pra_o & cia->ddra);
        break;
    case CIAPRB:
        val = (cia->prb_i & ~cia->ddrb) | (cia->prb_o & cia->ddrb);
        break;
    case CIADDRA:
        val = cia->ddra;
        break;
    case CIADDRB:
        val = cia->ddrb;
        break;
    case CIATALO:
        val = cia->ta_latch;
        break;
    case CIATAHI:
        val = cia->ta_latch >> 8;
        break;
    case CIATBLO:
        val = cia->tb_latch;
        break;
    case CIATBHI:
        val = cia->tb_latch >> 8;
        break;
    case CIAICR:
        val = cia->icrr;
        cia->icrr = 0;
        break;
    case CIACRA:
        val = cia->cra;
        break;
    case CIACRB:
        val = cia->crb;
        break;
    default:
        log_error("Ignoring read from %s.%s\n",
                  cia_name(cia,s), cia_reg_name[off]);
        break;
    }

    if (val || (off != CIAICR)) /* ignore no-op icr reads */
        log_info("%s.%s: read %02x", cia_name(cia,s), cia_reg_name[off], val);

    return val;
}

void cia_set_icr_flag(
    struct amiga_state *s, struct cia *cia, uint8_t bit)
{
    log_info("%s.icr: set bit %u", cia_name(cia,s), bit);
    cia->icrr |= 1u << bit;
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
