/*
 * logging.h
 * 
 * Written in 2011 by Keir Fraser
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

enum subsystem {
    subsystem_main,
    subsystem_cia,
    subsystem_disk,
    subsystem_mem
};

enum loglevel {
    loglevel_info,
    loglevel_warn,
    loglevel_error,
    loglevel_none
};

struct amiga_state;

void __log_info(struct amiga_state *, enum subsystem, const char *, ...);
void __log_warn(struct amiga_state *, enum subsystem, const char *, ...);
void __log_error(struct amiga_state *, enum subsystem, const char *, ...);

#define _log_info(sub, f, a...) __log_info(s, sub, f, ##a)
#define _log_warn(sub, f, a...) __log_warn(s, sub, f, ##a)
#define _log_error(sub, f, a...) __log_error(s, sub, f, ##a)

#define log_info(f, a...) _log_info(SUBSYSTEM, f, ##a)
#define log_warn(f, a...) _log_warn(SUBSYSTEM, f, ##a)
#define log_error(f, a...) _log_error(SUBSYSTEM, f, ##a)

void logging_init(struct amiga_state *);

#endif /* __LOGGING_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
