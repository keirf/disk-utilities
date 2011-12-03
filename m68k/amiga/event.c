/******************************************************************************
 * event.c
 * 
 * Discrete-event simulation.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <err.h>
#include <stdlib.h>
#include <amiga/amiga.h>

struct event {
    time_ns_t time;
    void (*cb)(void *);
    void *cb_data;
    struct event *next;
    struct event_base *base;
};

static void remove_from_list(struct event **pprev, struct event *e)
{
    struct event *curr, **_pprev = pprev;

    while ((curr = *_pprev) != e)
        _pprev = &curr->next;

    *_pprev = e->next;
}

static void add_to_list(struct event **pprev, struct event *e)
{
    struct event *curr, **_pprev = pprev;

    while (((curr = *_pprev) != NULL) && (curr->time <= e->time))
        _pprev = &curr->next;

    e->next = curr;
    *_pprev = e;
}

struct event *event_alloc(
    struct event_base *base, void (*cb)(void *), void *cb_data)
{
    struct event *event = memalloc(sizeof(*event));
    event->cb = cb;
    event->cb_data = cb_data;
    event->time = 0;
    event->next = NULL;
    event->base = base;
    return event;
}

void event_destroy(struct event *event)
{
    event_unset(event);
    memfree(event);
}

void event_set(struct event *event, time_ns_t time)
{
    event_unset(event);
    event->time = time;
    add_to_list(&event->base->active_events, event);
}

void event_set_delta(struct event *event, time_ns_t delta)
{
    event_set(event, event->base->current_time + delta);
}

void event_unset(struct event *event)
{
    if (!event->time)
        return;
    remove_from_list(&event->base->active_events, event);
    event->time = 0;
}

void fire_events(struct event_base *base)
{
    struct event *event;

    while (((event = base->active_events) != NULL) &&
           (event->time <= base->current_time)) {
        base->active_events = event->next;
        event->time = 0;
        (*event->cb)(event->cb_data);
    }
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
