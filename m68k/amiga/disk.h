/*
 * disk.h
 * 
 * Amiga disk handling.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __DISK_H__
#define __DISK_H__

struct amiga_state;
#include <amiga/event.h>

enum motor_state {
    motor_off, motor_spinning_up, motor_spinning_down, motor_on
};

enum step_state {
    step_none, step_in, step_out
};

struct amiga_disk {
    struct event *motor_delay;
    enum motor_state motor;

    struct event *step_delay;
    enum step_state step;

    uint8_t old_ciabb;

    uint16_t tracknr;

    struct disk *df0_disk;
    struct track_raw *track_raw;
    unsigned int av_ns_per_cell;

    struct event *data_delay;
    time_ns_t last_bitcell_time;
    unsigned int data_word_bitpos, ns_per_cell;
    unsigned int input_pos, input_byte;
    uint16_t data_word;

    uint8_t dma;
    uint16_t dsklen;
};

void disk_init(struct amiga_state *);
void disk_cia_changed(struct amiga_state *);
void disk_dsklen_changed(struct amiga_state *);

#endif /* __DISK_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
