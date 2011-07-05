/******************************************************************************
 * mfmparse.c
 * 
 * Read a raw disk file from (Amiga program) diskread.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <utime.h>

#include <libdisk/stream.h>
#include <libdisk/disk.h>

int main(int argc, char **argv)
{
    unsigned int i;
    struct stream *s;
    struct disk *d;
    struct disk_info *di;
    struct track_info *ti;
    const char *prev_name;
    unsigned int st = 0;

    if ( argc != 3 )
        errx(1, "Usage: mfmparse <in> [<out>]");

    if ( (s = stream_open(argv[1])) == NULL )
        errx(1, "Failed to probe input file: %s", argv[1]);

    if ( (d = disk_create(argv[2])) == NULL )
        errx(1, "Unable to create new disk file: %s", argv[2]);

    di = disk_get_info(d);

    for ( i = 0; i < 160; i++ )
        track_write_mfm_from_stream(d, i, s);

    for ( i = 1; i < 160; i++ )
    {
        unsigned int j;
        ti = &di->track[i];
        for ( j = 0; j < ti->nr_sectors; j++ )
            if ( !(ti->valid_sectors & (1u << j)) )
                break;
        if ( j == ti->nr_sectors )
            continue;
        printf("T%u: sectors ", i);
        for ( j = 0; j < ti->nr_sectors; j++ )
            if ( !(ti->valid_sectors & (1u << j)) )
                printf("%u,", j);
        printf(" missing\n");
    }

#if 1
    prev_name = di->track[0].typename;
    for ( i = 1; i <= 160; i++ )
    {
        ti = &di->track[i];
        if ( (ti->typename == prev_name) && (i != 160) )
            continue;
        if ( st == i-1 )
            printf("T");
        else
            printf("T%u-", st);
        printf("%u: %s\n", i-1, di->track[i-1].typename);
        st = i;
        prev_name = di->track[i].typename;
    }
#endif

    disk_close(d);
    stream_close(s);

    return 0;
}
