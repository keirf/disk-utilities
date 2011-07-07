/******************************************************************************
 * mfmparse/config.h
 * 
 * Parse config file which defines allowed formats for particular disks.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <libdisk/disk.h>
#include <libdisk/util.h>

#include "common.h"

#define NR_TRACKS 160

#define DEF_DIR PREFIX "/share/mfmparse"
#define DEF_FIL "mfmparse_formats"

struct token {
    enum { STR, NUM, CHR, EOL } type;
    union {
        char str[128];
        struct {
            unsigned int start, end;
        } num;
        int ch;
    } u;
};

static struct file_info {
    FILE *f;
    char *name;
    unsigned int line;
    struct file_info *next;
} *fi;

static void parse_err(const char *f, ...)
{
    char errs[128];
    va_list args;

    va_start(args, f);
    vsnprintf(errs, sizeof(errs), f, args);
    va_end(args);

    errx(1, "error at %s:%u: %s", fi->name, fi->line, errs);
}

static int mygetc(void)
{
    int c = fgetc(fi->f);
    if ( c == '\n' )
        fi->line++;
    return c;
}

static void myungetc(int c)
{
    if ( c == '\n' )
        fi->line--;
    ungetc(c, fi->f);
}

static void parse_token(struct token *t)
{
    int c;

    while ( isspace(c = mygetc()) && (c != '\n') )
        continue;

retry:
    if ( isdigit(c) )
    {
        t->type = NUM;
        t->u.num.start = c - '0';
        while ( isdigit(c = mygetc()) )
            t->u.num.start = t->u.num.start * 10 + c - '0';
        t->u.num.end = t->u.num.start;
        if ( c == '-' )
        {
            t->u.num.end = 0;
            while ( isdigit(c = mygetc()) )
                t->u.num.end = t->u.num.end * 10 + c - '0';
            if ( t->u.num.end < t->u.num.start )
                parse_err("bad range %u-%u", t->u.num.start, t->u.num.end);
        }
        myungetc(c);
    }
    else if ( c == '"' )
    {
        char *p = t->u.str;
        t->type = STR;
        while ( (c = mygetc()) != '"' )
        {
            if ( (c == '\n') || (c == '\r') || (c == EOF) )
                parse_err("unexpected newline or end-of-file in string");
            *p++ = c;
            if ( (p - t->u.str) >= (sizeof(t->u.str)-1) )
                parse_err("string too long");
        }
        *p = '\0';
    }
    else if ( isalpha(c) )
    {
        char *p = t->u.str;
        t->type = STR;
        *p++ = c;
        while ( isalnum(c = mygetc()) || (c == '_') )
        {
            *p++ = c;
            if ( (p - t->u.str) >= (sizeof(t->u.str)-1) )
                parse_err("string too long");
        }
        *p = '\0';
        myungetc(c);
    }
    else if ( c == '\\' )
    {
        while ( isspace(c = mygetc()) )
            continue;
        goto retry;
    }
    else if ( (c == EOF) || (c == '\n') )
    {
        t->type = EOL;
        t->u.ch = c;
    }
    else
    {
        t->type = CHR;
        t->u.ch = c;
    }
}

static struct file_info *open_file(char *name)
{
    struct file_info *fi = memalloc(sizeof(*fi));

    memset(fi, 0, sizeof(*fi));    
    fi->line = 1;

    if ( name[0] != '/' )
    {
        char *path;
        if ( (path = getcwd(NULL, 0)) == NULL )
            err(1, NULL);
        fi->name = memalloc(strlen(path) + strlen(name) + 2);
        sprintf(fi->name, "%s/%s", path, name);
        free(path);
        if ( (fi->f = fopen(fi->name, "r")) == NULL )
        {
            memfree(fi->name);
            fi->name = memalloc(strlen(DEF_DIR) + strlen(name) + 2);
            sprintf(fi->name, "%s/%s", DEF_DIR, name);
            fi->f = fopen(fi->name, "r");
        }
    }
    else
    {
        fi->name = memalloc(strlen(name) + 1);
        strcpy(fi->name, name);
        fi->f = fopen(fi->name, "r");
    }

    if ( fi->f == NULL )
    {
        memfree(fi->name);
        memfree(fi);
        fi = NULL;
    }

    return fi;
}

void close_file(struct file_info *fi)
{
    if ( fi->f != NULL )
        fclose(fi->f);
    memfree(fi->name);
    memfree(fi);
}

struct format_list *realloc_format_list(struct format_list *old)
{
    struct format_list *list;
    unsigned int max = old ? old->max*2 : 4;
    list = memalloc(sizeof(*list) + (max-1)*2);
    memset(list, 0, sizeof(*list));
    if ( old )
        memcpy(list, old, sizeof(*list) + (old->max-1)*2);
    list->max = max;
    return list;
}

struct format_list **parse_config(char *config, char *specifier)
{
    unsigned int i;
    struct format_list **formats;
    struct token t;
    char *spec;

    formats = memalloc(NR_TRACKS * sizeof(*formats));
    memset(formats, 0, NR_TRACKS * sizeof(*formats));

    if ( specifier == NULL )
        specifier = "default";

    spec = memalloc(strlen(specifier)+1);
    strcpy(spec, specifier);

    if ( (fi = open_file(config ? : DEF_FIL)) == NULL )
        errx(1, "could not open config file \"%s\"", config ? : DEF_FIL);

    for ( ; ; )
    {
        parse_token(&t);
        if ( (t.type == EOL) && (t.u.ch == EOF) )
        {
            struct file_info *fi2 = fi->next;
            if ( fi2 == NULL )
                parse_err("no match for \"%s\"", spec);
            close_file(fi);
            fi = fi2;
        }
        else if ( t.type != STR )
            /* nothing */;
        else if ( !strcmp("INCLUDE", t.u.str) )
        {
            struct file_info *fi2;
            parse_token(&t);
            if ( t.type != STR )
                parse_err("expected string after INCLUDE");
            if ( (fi2 = open_file(t.u.str)) == NULL )
                parse_err("could not open config file \"%s\"", t.u.str);
            fi2->next = fi;
            fi = fi2;
            t.type = EOL;
        }
        else if ( !strcmp(spec, t.u.str) )
        {
            parse_token(&t);
            if ( (t.type == CHR) && (t.u.ch == '=') )
            {
                parse_token(&t);
                if ( t.type != STR )
                    parse_err("expected string after =");
                if ( verbose )
                    printf("Format \"%s\"->\"%s\"\n", spec, t.u.str);
                memfree(spec);
                spec = memalloc(strlen(t.u.str+1));
                strcpy(spec, t.u.str);
            }
            else
            {
                goto found;
            }
        }
        while ( t.type != EOL )
            parse_token(&t);
    }

found:
    if ( verbose )
        printf("Found format \"%s\"\n", spec);
    for ( ; ; )
    {
        const char *fmtname;
        unsigned int start, end;
        struct format_list *list = realloc_format_list(NULL);

        while ( t.type != EOL )
            parse_token(&t);
        parse_token(&t);
        if ( (t.type == CHR) && (t.u.ch == '*') )
        {
            t.type = NUM;
            t.u.num.start = 0;
            t.u.num.end = 159;
        }
        if ( t.type != NUM )
            break;
        start = t.u.num.start;
        end = t.u.num.end;
        if ( (start >= NR_TRACKS) || (end >= NR_TRACKS) )
            parse_err("bad track range %u-%u", start, end);
        for ( ; ; )
        {
            parse_token(&t);
            if ( t.type == EOL )
                break;
            if ( t.type != STR )
                parse_err("expected format string");
            for ( i = 0;
                  (fmtname = disk_get_format_id_name(i)) != NULL;
                  i++ )
                if ( !strcmp(fmtname, t.u.str) )
                    break;
            if ( fmtname == NULL )
                parse_err("bad format name \"%s\"", t.u.str);
            if ( list->nr == list->max )
                list = realloc_format_list(list);
            list->ent[list->nr++] = i;
        }
        if ( list->nr == 0 )
            parse_err("empty format list");
        for ( i = start; i <= end; i++ )
            if ( formats[i] == NULL )
                formats[i] = list;
    }

    for ( i = 0; i < NR_TRACKS; i++ )
        if ( formats[i] == NULL )
            parse_err("no format specified for track %u", i);

    memfree(spec);
    while ( fi != NULL )
    {
        struct file_info *fi2 = fi->next;
        close_file(fi);
        fi = fi2;
    }

    return formats;
}
