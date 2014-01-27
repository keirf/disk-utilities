/*
 * custom.c
 * 
 * Miscellaneous Amiga custom chip handling.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <amiga/amiga.h>
#include <amiga/custom.h>

#define SUBSYSTEM subsystem_main

void custom_write_reg(struct amiga_state *s, uint16_t addr, uint16_t val)
{
    addr >>= 1;
    if (addr >= ARRAY_SIZE(custom_reg_name))
        return;

    switch (addr) {
    case CUST_dsklen:
        s->custom[addr] = val;
        disk_dsklen_changed(s);
        break;
    case CUST_dmacon:
    case CUST_intena:
    case CUST_intreq:
    case CUST_adkcon:
        if (val & 0x8000)
            s->custom[addr] |= val & 0x7fff;
        else
            s->custom[addr] &= ~val;
        break;
    default:
        s->custom[addr] = val;
        break;
    }

    log_info("Write %04x to custom register %s (%x) becomes %04x",
             val, custom_reg_name[addr], (addr<<1)+0xdff000,
             s->custom[addr]);
}

uint16_t custom_read_reg(struct amiga_state *s, uint16_t addr)
{
    uint16_t val = 0xffff;

    addr >>= 1;
    if (addr >= ARRAY_SIZE(custom_reg_name))
        return val;

    switch (addr) {
    case CUST_dmaconr:
        val = s->custom[CUST_dmacon];
        break;
    case CUST_adkconr:
        val = s->custom[CUST_adkcon];
        break;
    case CUST_intenar:
        val = s->custom[CUST_intena];
        break;
    case CUST_intreqr:
        val = s->custom[CUST_intreq];
        break;
    case CUST_dskbytr:
        val = s->custom[CUST_dskbytr];
        s->custom[CUST_dskbytr] &= 0x7fff;
        break;
    default:
        val = s->custom[addr];
        break;
    }

    if ((addr != CUST_dskbytr) && (addr != CUST_intreqr))
        log_info("Read %04x from custom register %s (%x)",
                 val, custom_reg_name[addr], (addr<<1)+0xdff000);

    return val;
}

void intreq_set_bit(struct amiga_state *s, uint8_t bit)
{
    if (!(s->custom[CUST_intreq] & (1u<<bit)))
        log_info("INTREQ bit %u set", bit);
    s->custom[CUST_intreq] |= 1u << bit;
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
