/******************************************************************************
 * custom.h
 * 
 * Miscellaneous Amiga custom chip handling.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __AMIGA_CUSTOM_H__
#define __AMIGA_CUSTOM_H__

void custom_write_reg(struct amiga_state *s, uint16_t addr, uint16_t val);
uint16_t custom_read_reg(struct amiga_state *s, uint16_t addr);

void intreq_set_bit(struct amiga_state *s, uint8_t bit);

const char *custom_reg_name[243];

#define CUST_dmaconr  (0x02/2)
#define CUST_adkconr  (0x10/2)
#define CUST_dskbytr  (0x1a/2)
#define CUST_intenar  (0x1c/2)
#define CUST_intreqr  (0x1e/2)
#define CUST_dskpth   (0x20/2)
#define CUST_dskptl   (0x22/2)
#define CUST_dsklen   (0x24/2)
#define CUST_dskdat   (0x26/2)
#define CUST_dsksync  (0x7e/2)
#define CUST_dmacon   (0x96/2)
#define CUST_intena   (0x9a/2)
#define CUST_intreq   (0x9c/2)
#define CUST_adkcon   (0x9e/2)

#endif /* __AMIGA_CUSTOM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
