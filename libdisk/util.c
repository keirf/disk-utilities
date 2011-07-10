/******************************************************************************
 * util.c
 * 
 * Little helper utils.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>

#include <errno.h>
#include <unistd.h>

void *memalloc(size_t size)
{
    void *p = malloc(size);
    if ( p == NULL ) err(1, NULL);
    return p;
}

void memfree(void *p)
{
    free(p);
}

void read_exact(int fd, void *buf, size_t count)
{
    ssize_t done;
    char *_buf = buf;

    while ( count > 0 )
    {
        done = read(fd, _buf, count);
        if ( (done < 0) && ((errno == EAGAIN) || (errno == EINTR)) )
            done = 0;
        if ( done < 0 )
            err(1, NULL);
        if ( done == 0 )
        {
            memset(_buf, 0, count);
            done = count;
        }
        count -= done;
        _buf += done;
    }
}

void write_exact(int fd, const void *buf, size_t count)
{
    ssize_t done;
    const char *_buf = buf;

    while ( count > 0 )
    {
        done = write(fd, _buf, count);
        if ( (done < 0) && ((errno == EAGAIN) || (errno == EINTR)) )
            done = 0;
        if ( done < 0 )
            err(1, NULL);
        count -= done;
        _buf += done;
    }
}
