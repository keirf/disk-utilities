/*
 * logging.c
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdarg.h>
#include <stdio.h>

#include <amiga/amiga.h>

static const char *subsys_name[] = {
    [subsystem_main] = "Main",
    [subsystem_cia] = "CIA",
    [subsystem_disk] = "Disk",
    [subsystem_mem] = "Mem"
};

static void write_log(
    struct amiga_state *s,
    enum loglevel loglevel,
    enum subsystem subsystem,
    const char *fmt, va_list args)
{
    if (loglevel < s->max_loglevel)
        return;
    fprintf(s->logfile, "[%s,PC=%08x,%u.%03uus] ",
            subsys_name[subsystem], s->ctxt.regs->pc,
            (unsigned int)(s->event_base.current_time/1000),
            (unsigned int)(s->event_base.current_time%1000));
    (void)vfprintf(s->logfile, fmt, args);
    fprintf(s->logfile, "\n");
}

void logging_init(struct amiga_state *s)
{
    s->max_loglevel = loglevel_info;
    s->logfile = stderr;
}

#define LOG(lvl)                                                        \
void __log_##lvl(struct amiga_state *s, enum subsystem subsystem,       \
                 const char *fmt, ...)                                  \
{                                                                       \
    va_list args;                                                       \
    va_start(args, fmt);                                                \
    write_log(s, loglevel_##lvl, subsystem, fmt, args);                 \
    va_end(args);                                                       \
}
LOG(info)
LOG(warn)
LOG(error)

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
