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
    struct disk_header *dh;
    struct track_header *th;
    unsigned int prev_type = ~0u, st = 0;

    if ( argc != 3 )
        errx(1, "Usage: mfmparse <in> [<out>]");

    if ( (s = stream_open(argv[1])) == NULL )
        errx(1, "Failed to probe input file: %s", argv[1]);

    if ( (d = disk_create(argv[2])) == NULL )
        errx(1, "Unable to create new disk file: %s", argv[2]);

    dh = disk_get_header(d);

    for ( i = 0; i < 160; i++ )
        track_write_mfm_from_stream(d, i, s);

    for ( i = 1; i < 160; i++ )
    {
        uint32_t valid_sectors;
        unsigned int j;
        th = &dh->track[i];
        valid_sectors = track_valid_sector_map(th);
        for ( j = 0; j < th->nr_sectors; j++ )
            if ( !(valid_sectors & (1u << j)) )
                break;
        if ( j == th->nr_sectors )
            continue;
        printf("T%u: sectors ", i);
        for ( j = 0; j < th->nr_sectors; j++ )
            if ( !(valid_sectors & (1u << j)) )
                printf("%u,", j);
        printf(" missing\n");
    }

#if 1
    prev_type = dh->track[0].type;
    for ( i = 1; i <= 160; i++ )
    {
        if ( (dh->track[i].type == prev_type) && (i != 160) )
            continue;
        if ( st == i-1 )
            printf("T");
        else
            printf("T%u-", st);
        printf("%u: %s\n", i-1, track_type_name(d, i-1));            
        st = i;
        prev_type = dh->track[i].type;
    }
    
#if 0
    for ( i = 0; i < 160; i++ )
        printf("%u: %u %u\n", i, dh->track[i].data_bitoff, 
               dh->track[i].total_bits);
#endif
#endif

    disk_close(d);
    stream_close(s);

    return 0;
}
