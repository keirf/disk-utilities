/******************************************************************************
 * event.h
 * 
 * Discrete-event simulation.
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdint.h>

/* An absolute or delta time, in nanoseconds. */
typedef uint64_t time_ns_t;

#define MICROSECS(x) ((x) * 1000ull)
#define MILLISECS(x) ((x) * 1000000ull)

struct event;

struct event_base {
    /* Absolute time since simulation start. */
    time_ns_t current_time;
    /* [Private] list of registered events. */
    struct event *active_events;
};

struct event *event_alloc(
    struct event_base *base, void (*cb)(void *), void *cb_data);
void event_destroy(struct event *event);

void event_set(struct event *event, time_ns_t time);
void event_set_delta(struct event *event, time_ns_t delta);
void event_unset(struct event *event);

void fire_events(struct event_base *base);

#endif /* __EVENT_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
