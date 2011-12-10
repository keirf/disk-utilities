/*
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
#include <getopt.h>

#include <libdisk/stream.h>
#include <libdisk/disk.h>

#include "common.h"

int quiet, verbose;

static void usage(int rc)
{
    printf("Usage: mfmparse [options] in_file out_file\n");
    printf("Options:\n");
    printf("  -h, --help    Display this information\n");
    printf("  -q, --quiet   Quiesce normal informational output\n");
    printf("  -v, --verbose Print extra diagnostic info\n");
    printf("  -f, --format=FORMAT Name of format descriptor in config file\n");
    printf("  -c, --config=FILE   Config file to parse for format info\n");
    printf("Supported file formats (suffix => type):\n");
    printf("  .adf => ADF\n");
    printf("  .ipf => SPS/IPF\n");
    printf("  .dat => Diskread (read-only)\n");
    printf("  .dsk => Libdisk\n");
    printf("  .raw => Kryoflux STREAM (read-only)\n");

    exit(rc);
}

int main(int argc, char **argv)
{
    unsigned int i;
    struct stream *s;
    struct disk *d;
    struct disk_info *di;
    struct track_info *ti;
    struct format_list **format_lists;
    const char *prev_name;
    char *config = NULL, *format = NULL;
    unsigned int st = 0, unidentified = 0;
    int ch;

    const static char sopts[] = "hqvf:c:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "verbose", 0, NULL, 'v' },
        { "format", 1, NULL, 'f' },
        { "config",  1, NULL, 'c' },
        { 0, 0, 0, 0}
    };

    while ((ch = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (ch) {
        case 'h':
            usage(0);
            break;
        case 'q':
            quiet = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'f':
            format = optarg;
            break;
        case 'c':
            config = optarg;
            break;
        default:
            usage(1);
            break;
        }
    }

    if (argc != (optind + 2))
        usage(1);

    format_lists = parse_config(config, format);

    if ((s = stream_open(argv[optind])) == NULL)
        errx(1, "Failed to probe input file: %s", argv[optind]);

    if ((d = disk_create(argv[optind+1])) == NULL)
        errx(1, "Unable to create new disk file: %s", argv[optind+1]);

    di = disk_get_info(d);

    for (i = 0; i < 160; i++) {
        struct format_list *list = format_lists[i];
        unsigned int j;
        if (list == NULL)
            continue;
        for (j = 0; j < list->nr; j++) {
            if (track_write_mfm_from_stream(
                    d, i, list->ent[list->pos], s) == 0)
                break;
            if (++list->pos >= list->nr)
                list->pos = 0;
        }
        if ((j == list->nr) &&
            (track_write_mfm_from_stream(d, i, TRKTYP_unformatted, s) != 0))
            unidentified++;
    }

    for (i = 1; i < 160; i++) {
        unsigned int j;
        ti = &di->track[i];
        for (j = 0; j < ti->nr_sectors; j++)
            if (!(ti->valid_sectors & (1u << j)))
                break;
        if (j == ti->nr_sectors)
            continue;
        unidentified++;
        printf("T%u: sectors ", i);
        for (j = 0; j < ti->nr_sectors; j++)
            if (!(ti->valid_sectors & (1u << j)))
                printf("%u,", j);
        printf(" missing\n");
    }

    if (quiet)
        goto out;

    prev_name = di->track[0].typename;
    for (i = 1; i <= 160; i++) {
        ti = &di->track[i];
        if ((ti->typename == prev_name) && (i != 160))
            continue;
        if (st == i-1)
            printf("T");
        else
            printf("T%u-", st);
        printf("%u: %s\n", i-1, di->track[i-1].typename);
        st = i;
        prev_name = di->track[i].typename;
    }

out:
    if (unidentified)
        fprintf(stderr,"** WARNING: %u tracks are damaged or unidentified!\n",
                unidentified);

    disk_close(d);
    stream_close(s);

    return 0;
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
