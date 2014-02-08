/*
 * disk.c
 * 
 * Amiga disk handling.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <amiga/amiga.h>
#include <amiga/custom.h>

#include <libdisk/disk.h>

#define SUBSYSTEM subsystem_disk

static const char *df0_filename =
    "/home/keir/Amiga/raw_dumps/newzealandstory/nzs.dat";

#define STEP_DELAY     MILLISECS(1)
#define MOTORON_DELAY  MILLISECS(100)
#define MOTOROFF_DELAY MILLISECS(1)

static void track_load_byte(struct amiga_state *s)
{
    s->disk.ns_per_cell =
        (s->disk.av_ns_per_cell *
         s->disk.track_raw->speed[s->disk.input_pos/8]) / 1000u;
    s->disk.input_byte = s->disk.track_raw->bits[s->disk.input_pos/8];
}

static void disk_dma_word(struct amiga_state *s, uint16_t w)
{
    if (s->disk.dsklen & 0x3fff) {
        uint32_t dskpt =
            (s->custom[CUST_dskpth] << 16) | s->custom[CUST_dskptl];
        s->ctxt.ops->write(dskpt, w, 2, &s->ctxt);
        dskpt += 2;
        s->custom[CUST_dskpth] = dskpt >> 16;
        s->custom[CUST_dskptl] = dskpt;
        s->disk.dsklen--;
    }

    if (!(s->disk.dsklen & 0x3fff)) {
        log_info("Disk DMA finished");
        s->disk.dma = 0;
        intreq_set_bit(s, 1); /* disk block done */
    }
}

static void data_cb(void *_s)
{
    struct amiga_state *s = _s;
    time_ns_t t = s->disk.last_bitcell_time;
    time_ns_t now = s->event_base.current_time;
    uint16_t w = s->disk.data_word;

    for (t += s->disk.ns_per_cell; t <= now; t += s->disk.ns_per_cell) {
        w <<= 1;
        if (s->disk.input_byte & 0x80)
            w |= 1;
        s->disk.input_byte <<= 1;
        if (++s->disk.input_pos == s->disk.track_raw->bitlen) {
            cia_set_icr_flag(s, &s->ciab, CIAICRB_FLG);
            s->disk.input_pos = 0;
        }
        if (!(s->disk.input_pos & 7))
            track_load_byte(s);
        s->disk.data_word_bitpos++;
        s->custom[CUST_dskbytr] &= ~(1u<<12);
        if (!(s->disk.data_word_bitpos & 7)) {
            s->custom[CUST_dskbytr] &= 0x7f00;
            s->custom[CUST_dskbytr] |= 0x8000 | (uint8_t)w;
            if ((s->disk.dma == 2) && !(s->disk.data_word_bitpos & 15))
                disk_dma_word(s, w);
        }
        if ((s->custom[CUST_adkcon] & (1u<<10)) /* WORDSYNC? */
            && (w == s->custom[CUST_dsksync])) {
            log_info("Disk sync found");
            intreq_set_bit(s, 12); /* disk sync found */
            s->custom[CUST_dskbytr] |= 1u<<12; /* WORDEQUAL */
            s->disk.data_word_bitpos = 0;
            if ((s->custom[CUST_dmacon] & (1u<<4)) && (s->disk.dma == 1)) {
                /* How much checking should I do for DMA read start? RNC 
                 * Copylock only sets dmacon[4], doesn't touch the master 
                 * enable (dmacon[9]). UAE doesn't check DMACON at all for 
                 * disk read DMAs. I check dmacon[4] only for now. */
                log_info("Disk DMA started");
                /* Note that DMA fetch begins with the *next* full word of MFM 
                 * streamed from disk (i.e., toss the first sync word). */
                s->disk.dma = 2;
            }
        }
    }

    s->disk.last_bitcell_time = t - s->disk.ns_per_cell;
    s->disk.data_word = w;

    event_set(s->disk.data_delay, t);
}

static void track_load(struct amiga_state *s)
{
    log_info("Loading track %u", s->disk.tracknr);
    track_read_raw(s->disk.track_raw, s->disk.tracknr);
    s->disk.input_pos = s->disk.data_word_bitpos = s->disk.data_word = 0;
    s->disk.last_bitcell_time = s->event_base.current_time;
    s->disk.av_ns_per_cell = 200000000ul / s->disk.track_raw->bitlen;
    track_load_byte(s);
    data_cb(s);
}

static void track_unload(struct amiga_state *s)
{
    track_purge_raw_buffer(s->disk.track_raw);
    event_unset(s->disk.data_delay);
}

static void disk_recalc_cia_inputs(struct amiga_state *s)
{
    s->ciaa.pra_i |= 0x3c;
    if (s->ciab.prb_o & (1u << CIAB_DSKSEL0))
        return;

    switch (s->disk.motor) {
    case motor_off:
    case motor_spinning_up:
        s->ciaa.pra_i |= 1u << CIAB_DSKRDY;
        break;
    case motor_on:
    case motor_spinning_down:
        s->ciaa.pra_i &= ~(1u << CIAB_DSKRDY);
        break;
    }

    if (s->disk.tracknr <= 1)
        s->ciaa.pra_i &= ~(1u << CIAB_DSKTRACK0);
}

static void motor_cb(void *_s)
{
    struct amiga_state *s = _s;
    if (s->disk.motor == motor_spinning_up) {
        log_info("Disk motor on and fully spun up");
        s->disk.motor = motor_on;
        track_load(s);
    } else {
        log_info("Disk motor off and fully spun down");
        s->disk.motor = motor_off;
        track_unload(s);
    }
    disk_recalc_cia_inputs(s);
}

static void step_cb(void *_s)
{
    struct amiga_state *s = _s;
    if (s->disk.step == step_in)
        s->disk.tracknr += 2;
    else
        s->disk.tracknr -= 2;
    s->disk.step = step_none;
    track_load(s);
    disk_recalc_cia_inputs(s);
}

void disk_cia_changed(struct amiga_state *s)
{
    uint8_t new_ciabb = s->ciab.prb_o;
    uint8_t old_ciabb = s->disk.old_ciabb;

    /* Disk side. */
    if ((old_ciabb ^ new_ciabb) & (1u << CIAB_DSKSIDE)) {
        s->disk.tracknr ^= 1;
        track_load(s);
    }

    /* Skip most of this if DF0: not selected. */
    if (new_ciabb & (1u << CIAB_DSKSEL0))
        goto out;

    /* Latch motor state on disk-selection edge. */
    if (old_ciabb & (1u << CIAB_DSKSEL0)) {
        if (!(new_ciabb & (1u << CIAB_DSKMOTOR))) {
            if (s->disk.motor == motor_off) {
                log_info("Disk spinning up");
                s->disk.motor = motor_spinning_up;
                event_set_delta(s->disk.motor_delay, MOTORON_DELAY);
            } else if (s->disk.motor == motor_spinning_down) {
                log_warn("Disk spindown aborted");
                s->disk.motor = motor_on;
                event_unset(s->disk.motor_delay);
            }
        } else {
            if (s->disk.motor == motor_on) {
                log_info("Disk spinning down");
                s->disk.motor = motor_spinning_down;
                event_set_delta(s->disk.motor_delay, MOTOROFF_DELAY);
            } else if (s->disk.motor == motor_spinning_up) {
                log_warn("Disk spinup aborted");
                s->disk.motor = motor_off;
                event_unset(s->disk.motor_delay);
            }
        }
    }

    /* Disk step request? */
    if (!(old_ciabb & (1u << CIAB_DSKSTEP)) &&
        (new_ciabb & (1u << CIAB_DSKSTEP)) &&
        (s->disk.step == step_none)) {
        s->disk.step = (new_ciabb & (1u << CIAB_DSKDIREC))
            ? step_out : step_in;
        if (((s->disk.step == step_out) && (s->disk.tracknr <= 1)) ||
            ((s->disk.step == step_in) && (s->disk.tracknr >= 159)))
            s->disk.step = step_none;
        if (s->disk.step != step_none)
            event_set_delta(s->disk.step_delay, STEP_DELAY);
    }

out:
    s->disk.old_ciabb = new_ciabb;
    disk_recalc_cia_inputs(s);
}

void disk_dsklen_changed(struct amiga_state *s)
{
    uint16_t old_dsklen = s->disk.dsklen;
    uint16_t new_dsklen = s->custom[CUST_dsklen];
    if ((old_dsklen & new_dsklen & 0x8000) && !s->disk.dma) {
        log_info("DSKLEN requests DMA start %04x", new_dsklen);
        s->disk.dma = 1;
    } else if (!(new_dsklen & 0x8000) && s->disk.dma) {
        log_warn("Disk DMA aborted, %u words left", old_dsklen & 0x3fff);
        s->disk.dma = 0;
    }
    s->disk.dsklen = new_dsklen;
}

void disk_init(struct amiga_state *s)
{
    s->disk.df0_disk = disk_open(df0_filename, 1);
    if (s->disk.df0_disk == NULL)
        errx(1, "%s", df0_filename);
    s->disk.track_raw = track_alloc_raw_buffer(s->disk.df0_disk);

    /* Set up CIA peripheral data registers. */
    s->ciaa.pra_i = 0xff; /* disk inputs, all off (active low) */
    s->ciaa.ddra = 0x03;
    s->ciab.prb_o = 0xff; /* disk outputs, all off (active low) */
    s->ciab.ddrb = 0xff;

    s->disk.motor_delay = event_alloc(&s->event_base, motor_cb, s);
    s->disk.motor = motor_off;
    s->disk.step_delay = event_alloc(&s->event_base, step_cb, s);
    s->disk.step = step_none;
    s->disk.old_ciabb = s->ciab.prb_o;
    s->disk.tracknr = 1;
    s->disk.data_delay = event_alloc(&s->event_base, data_cb, s);
}

void amiga_insert_df0(const char *filename)
{
    df0_filename = filename;
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
