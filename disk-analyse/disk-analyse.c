/*
 * disk-analyse/disk-analyse.c
 * 
 * Disk data analyser and transcoder.
 * 
 * Written in 2011-2012 by Keir Fraser
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <getopt.h>

#include <libdisk/stream.h>
#include <libdisk/disk.h>
#include <libdisk/util.h>

#include "common.h"

int quiet, verbose;
static int index_align;
static enum pll_mode pll_mode = PLL_default;
static struct format_list **format_lists;
static char *in, *out;

static void usage(int rc)
{
    printf("Usage: disk-analyse [options] in_file out_file\n");
    printf("Options:\n");
    printf("  -h, --help    Display this information\n");
    printf("  -q, --quiet   Quiesce normal informational output\n");
    printf("  -v, --verbose Print extra diagnostic info\n");
    printf("  -i, --index-align   Align all track starts near index mark\n");
    printf("  -p, --pll=MODE      MODE={fixed,variable,authentic}\n");
    printf("  -f, --format=FORMAT Name of format descriptor in config file\n");
    printf("  -c, --config=FILE   Config file to parse for format info\n");
    printf("Supported file formats (suffix => type):\n");
    printf("  .adf  => ADF\n");
    printf("  .eadf => Extended-ADF\n");
    printf("  .img  => IBM-MFM Sector Dump\n");
    printf("  .ipf  => SPS/IPF\n");
    printf("  .dsk  => Libdisk\n");
    printf("Read-only support:\n");
    printf("  .dat  => Diskread\n");
    printf("  .dfi  => DiscFerret DFE2\n");
    printf("  .raw  => Kryoflux STREAM\n");

    exit(rc);
}

static void dump_track_list(struct disk_info *di)
{
    unsigned int i, st = 0;
    const char *prev_name;
    struct track_info *ti;

    if (quiet)
        return;

    prev_name = di->track[0].typename;
    for (i = 1; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        if (ti->typename == prev_name)
            continue;
        if (st == i-1)
            printf("T");
        else
            printf("T%u-", st);
        printf("%u: %s\n", i-1, prev_name);
        st = i;
        prev_name = ti->typename;
    }
    if (st == i-1)
        printf("T");
    else
        printf("T%u-", st);
    printf("%u: %s\n", i-1, prev_name);
}

static void handle_stream(void)
{
    struct stream *s;
    struct disk *d;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, unidentified = 0;

    if ((s = stream_open(in)) == NULL)
        errx(1, "Failed to probe input file: %s", in);

    stream_pll_mode(s, pll_mode);

    if ((d = disk_create(out)) == NULL)
        errx(1, "Unable to create new disk file: %s", out);

    di = disk_get_info(d);

    for (i = 0; i < di->nr_tracks; i++) {
        struct format_list *list = format_lists[i];
        unsigned int j;
        if (list == NULL)
            continue;
        for (j = 0; j < list->nr; j++) {
            if (track_write_raw_from_stream(
                    d, i, list->ent[list->pos], s) == 0)
                break;
            if (++list->pos >= list->nr)
                list->pos = 0;
        }
        if ((j == list->nr) &&
            (track_write_raw_from_stream(d, i, TRKTYP_unformatted, s) != 0)) {
            /* Tracks 160+ are expected to be unused. Don't warn about them. */
            if (i < 160)
                unidentified++;
            else
                track_mark_unformatted(d, i);
        }
    }

    for (i = 0; i < di->nr_tracks; i++) {
        unsigned int j;
        ti = &di->track[i];
        if (index_align)
            ti->data_bitoff = 1024;
        for (j = 0; j < ti->nr_sectors; j++)
            if (!is_valid_sector(ti, j))
                break;
        if (j == ti->nr_sectors)
            continue;
        unidentified++;
        printf("T%u: sectors ", i);
        for (j = 0; j < ti->nr_sectors; j++)
            if (!is_valid_sector(ti, j))
                printf("%u,", j);
        printf(" missing\n");
    }

    dump_track_list(di);

    if (unidentified)
        fprintf(stderr,"** WARNING: %u tracks are damaged or unidentified!\n",
                unidentified);

    disk_close(d);
    stream_close(s);
}

static void handle_img(void)
{
    int fd;
    off_t sz;
    unsigned int i;
    struct track_sectors *sectors;
    struct disk *d;
    struct disk_info *di;

    if ((fd = file_open(in, O_RDONLY)) == -1)
        err(1, "Failed to open IMG file '%s'", in);
    sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if ((d = disk_create(out)) == NULL)
        errx(1, "Unable to create new disk file: %s", out);

    di = disk_get_info(d);

    sectors = track_alloc_sector_buffer(d);
    sectors->data = memalloc(sz);
    sectors->nr_bytes = sz;

    read_exact(fd, sectors->data, sz);
    close(fd);

    for (i = 0; (i < di->nr_tracks) && sectors->nr_bytes; i++) {
        struct format_list *list = format_lists[i];
        if ((list == NULL) || (list->nr == 0))
            continue;
        if (list->nr > 1)
            errx(1, "T%u: More than one format specified for IMG data", i);
        if (track_write_sectors(sectors, i, list->ent[0]) != 0)
            errx(1, "T%u: %s: Unable to import IMG data",
                 i, disk_get_format_desc_name(list->ent[0]));
    }

    if (sectors->nr_bytes != 0)
        errx(1, "Unexpected extra data at end of IMG file");

    dump_track_list(di);

    disk_close(d);

    memfree(sectors->data);
    sectors->data = NULL;
    sectors->nr_bytes = 0;
    track_free_sector_buffer(sectors);
}

int main(int argc, char **argv)
{
    char *p, *config = NULL, *format = NULL;
    int ch;

    const static char sopts[] = "hqvip:f:c:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "verbose", 0, NULL, 'v' },
        { "index-align", 0, NULL, 'i' },
        { "pll", 1, NULL, 'p' },
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
        case 'i':
            index_align = 1;
            break;
        case 'p':
            if (!strcmp(optarg, "fixed"))
                pll_mode = PLL_fixed_clock;
            else if (!strcmp(optarg, "variable"))
                pll_mode = PLL_variable_clock;
            else if (!strcmp(optarg, "authentic"))
                pll_mode = PLL_authentic;
            else {
                warnx("Unrecognised PLL mode '%s'", optarg);
                usage(1);
            }
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

    in = argv[optind];
    out = argv[optind+1];

    format_lists = parse_config(config, format);

    if (((p = strrchr(in, '.')) != NULL) && !strcmp(p+1, "img"))
        handle_img();
    else
        handle_stream();

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
