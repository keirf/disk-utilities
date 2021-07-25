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
static unsigned int start_cyl, disk_flags;
static int index_align, clear_bad_sectors, single_sided = -1, end_cyl = -1;
static int double_step = 0;
static unsigned int drive_rpm = 300, data_rpm = 300;
static int pll_period_adj_pct = -1, pll_phase_adj_pct = -1;
static struct format_list **format_lists;
static char *in, *out;

/* Iteration start/step for single- and double-sided modes. */
#define _TRACK_START ((single_sided == 1) ? 1 : 0)
#define TRACK_START (start_cyl*2 + _TRACK_START)
#define TRACK_END(di) (((end_cyl >= 0) && (end_cyl*2 < (di)->nr_tracks)) \
                       ? end_cyl*2+1 : (di)->nr_tracks-1)
#define TRACK_STEP ((single_sided == -1) ? 1 : 2)

/* Output track as cyl.head */
#define cyl(trk) ((trk)/2)
#define hd(trk) ((trk)&1)
#define TRACK_ARG(t) cyl(t),hd(t)

static void usage(int rc)
{
    printf("Usage: disk-analyse [options] in_file out_file\n");
    printf("Options:\n");
    printf("  -h, --help          Display this information\n");
    printf("  -q, --quiet         Quiesce normal informational output\n");
    printf("  -v, --verbose       Print extra diagnostic info\n");
    printf("  -i, --index-align   Align all track starts near index mark\n");
    printf("  -C, --clear-bad-sectors Clear bad sectors in output\n");
    printf("  -p, --pll-period-adj=PCT (PCT=0..100) PLL period adjustment\n");
    printf("  -P, --pll-phase-adj=PCT (PCT=0..100) PLL phase adjustment\n");
    printf("                      Amount observed flux affects PLL\n");
    printf("  -r, --rpm=DRIVE[:DATA] RPM of drive that created the input,\n");
    printf("                         Original recording RPM of data [300]\n");
    printf("  -D, --double-step   Double Step\n");
    printf("  -s, --start-cyl=N   Start cylinder\n");
    printf("  -e, --end-cyl=N     End cylinder\n");
    printf("  -S, --ss[=0|1]      Single-sided disk (default is side 0)\n");
    printf("  -k, --kryoflux-hack Fill empty tracks with prev track's data\n");
    printf("  -f, --format=FORMAT Name of format descriptor in config file\n");
    printf("  -c, --config=FILE   Config file to parse for format info\n");
    printf("Supported file formats (suffix => type):\n");
    printf("  .adf  => ADF\n");
    printf("  .eadf => Extended-ADF\n");
    printf("  .hfe  => HxC Floppy Emulator (orig,v3)\n");
    printf("  .imd  => ImageDisk\n");
    printf("  .img  => IBM-MFM Sector Dump\n");
    printf("  .ipf  => SPS/IPF\n");
    printf("  .dsk  => Libdisk\n");
    printf("  .scp  => Supercard Pro\n");
    printf("  .st   => Atari ST Sector Dump\n");
    printf("Read-only support:\n");
    printf("  .dat  => Diskread\n");
    printf("  .dfi  => DiscFerret DFE2\n");
    printf("  *.raw => Kryoflux STREAM\n");
    printf("  .{ct,ctr,raw} => CAPS/SPS CT Raw\n");
    printf("Write-only support:\n");
    printf("  .jv3  => JV3 TRS80 Emulator\n");

    exit(rc);
}

static int count_lines(struct disk *d, unsigned int start, unsigned int step)
{
    struct disk_info *di = disk_get_info(d);
    char name[128], prev_name[128];
    unsigned int i, st, lines = 1;

    i = st = start;
    track_get_format_name(d, i, prev_name, sizeof(name));
    while ((i += step) <= TRACK_END(di)) {
        track_get_format_name(d, i, name, sizeof(name));
        if (!strcmp(name, prev_name))
            continue;
        lines++;
        strcpy(prev_name, name);
    }

    return lines;
}

static void dump_by_head(struct disk *d)
{
    struct disk_info *di = disk_get_info(d);
    char name[128], prev_name[128];
    unsigned int i, st, hd, step = 2;

    for (hd = 0; hd < 2; hd++) {
        printf("Side %u:\n", hd);
        i = st = TRACK_START + hd;
        track_get_format_name(d, i, prev_name, sizeof(name));
        while ((i += step) <= TRACK_END(di)) {
            track_get_format_name(d, i, name, sizeof(name));
            if (!strcmp(name, prev_name))
                continue;
            printf(" T");
            if (st != i-step)
                printf("%u-", st/2);
            printf("%u: %s\n", (i-step)/2, prev_name);
            st = i;
            strcpy(prev_name, name);
        }
        printf(" T");
        if (st != i-step)
            printf("%u-", st/2);
        printf("%u: %s\n", (i-step)/2, prev_name);
    }
}

static void dump_track_list(struct disk *d)
{
    struct disk_info *di = disk_get_info(d);
    char name[128], prev_name[128];
    unsigned int i, st;

    if (quiet || (TRACK_START > TRACK_END(di)))
        return;

    if (TRACK_STEP == 1) {
        int lines, hd_lines;

        /* Default: print track-by-track */
        lines = count_lines(d, TRACK_START, 1);

        /* Alternative: head at a time */
        hd_lines = 2;
        hd_lines += count_lines(d, TRACK_START, 2);
        hd_lines += count_lines(d, TRACK_START+1, 2);        

        /* Which gives the more concise summary? */
        if (hd_lines < lines)
            return dump_by_head(d);
    }

    i = st = TRACK_START;
    track_get_format_name(d, i, prev_name, sizeof(name));
    while ((i += TRACK_STEP) <= TRACK_END(di)) {
        track_get_format_name(d, i, name, sizeof(name));
        if (!strcmp(name, prev_name))
            continue;
        printf("T");
        if (st != i-TRACK_STEP)
            printf("%u.%u-", TRACK_ARG(st));
        printf("%u.%u: %s\n", TRACK_ARG(i-TRACK_STEP), prev_name);
        st = i;
        strcpy(prev_name, name);
    }
    printf("T");
    if (st != i-TRACK_STEP)
        printf("%u.%u-", TRACK_ARG(st));
    printf("%u.%u: %s\n", TRACK_ARG(i-TRACK_STEP), prev_name);
}

static void probe_stream(void)
{
    struct stream *s;
    struct disk *d;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i;

    if ((s = stream_open(in, drive_rpm, data_rpm)) == NULL)
        errx(1, "Failed to probe input file: %s", in);
    s->double_step = double_step;

    if (pll_period_adj_pct >= 0)
        s->pll_period_adj_pct = pll_period_adj_pct;
    if (pll_phase_adj_pct >= 0)
        s->pll_phase_adj_pct = pll_phase_adj_pct;
    if (verbose)
        printf("PLL Parameters: period_adj=%d%% phase_adj=%d%%\n",
               s->pll_period_adj_pct, s->pll_phase_adj_pct);

    if ((d = disk_create(out, disk_flags | DISKFL_rpm(data_rpm))) == NULL)
        errx(1, "Unable to create new disk file: %s", out);
    di = disk_get_info(d);

    for (i = TRACK_START; i <= TRACK_END(di); i += TRACK_STEP) {
        unsigned int j, k, nr = 0;
        char name[128];
        const char *fmtname;
        printf("T%u.%u: ", TRACK_ARG(i));
        for (j = 0; (fmtname = disk_get_format_id_name(j)) != NULL; j++) {
            if (!strncmp(fmtname, "raw_", 4)) {
                /* Skip raw formats, they accept everything. */
                continue;
            }
            if (track_write_raw_from_stream(d, i, j, s) == 0) {
                track_get_format_name(d, i, name, sizeof(name));
                if (!strncmp(name, "AmigaDOS", 8)
                    && strcmp(fmtname, "amigados")) {
                    /* Skip umpteen variations on AmigaDOS. */
                    continue;
                }
                if (nr++)
                    printf(", ");
                printf("%s(%s)", name, fmtname);
                ti = &di->track[i];
                for (k = 0; k < ti->nr_sectors; k++)
                    if (!is_valid_sector(ti, k))
                        break;
                if (k != ti->nr_sectors)
                    printf("[%u/%u]", k, ti->nr_sectors);
            }
        }
        if (!nr)
            printf("Unidentified");
        printf("\n");
    }

    disk_close(d);
    stream_close(s);
}

static void handle_stream(void)
{
    struct stream *s;
    struct disk *d;
    struct disk_info *di;
    struct track_info *ti;
    unsigned int i, unidentified = 0, bad_secs = 0;

    if ((s = stream_open(in, drive_rpm, data_rpm)) == NULL)
        errx(1, "Failed to probe input file: %s", in);
    s->double_step = double_step;

    if (pll_period_adj_pct >= 0)
        s->pll_period_adj_pct = pll_period_adj_pct;
    if (pll_phase_adj_pct >= 0)
        s->pll_phase_adj_pct = pll_phase_adj_pct;
    if (verbose)
        printf("PLL Parameters: period_adj=%d%% phase_adj=%d%%\n",
               s->pll_period_adj_pct, s->pll_phase_adj_pct);

    if ((d = disk_create(out, disk_flags | DISKFL_rpm(data_rpm))) == NULL)
        errx(1, "Unable to create new disk file: %s", out);
    di = disk_get_info(d);

    for (i = TRACK_START; i <= TRACK_END(di); i += TRACK_STEP) {
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

    for (i = TRACK_START; i <= TRACK_END(di); i += TRACK_STEP) {
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
        printf("T%u.%u: sectors ", TRACK_ARG(i));
        for (j = 0; j < ti->nr_sectors; j++) {
            if (is_valid_sector(ti, j))
                continue;
            printf("%u,", j);
            bad_secs++;
        }
        printf(" missing\n");
        if (clear_bad_sectors)
            set_all_sectors_valid(ti);
    }

    if (clear_bad_sectors && bad_secs)
        printf("** %u bad sector%s fixed up\n",
               bad_secs, (bad_secs > 1) ? "s" : "");

    dump_track_list(d);

    if (unidentified)
        fprintf(stderr,"** WARNING: %u track%s damaged or unidentified!\n",
                unidentified, (unidentified > 1) ? "s are" : " is");

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
    void *data;

    if ((fd = file_open(in, O_RDONLY)) == -1) {
        if (errno == ENOENT) {
            /* Non-existent file could be a Kryoflux stream basename. */
            return handle_stream();
        }
        err(1, "Failed to open IMG file '%s'", in);
    }
    sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if ((d = disk_create(out, disk_flags | DISKFL_rpm(data_rpm))) == NULL)
        errx(1, "Unable to create new disk file: %s", out);
    di = disk_get_info(d);

    sectors = track_alloc_sector_buffer(d);
    sectors->data = data = memalloc(sz);
    sectors->nr_bytes = sz;

    read_exact(fd, sectors->data, sz);
    close(fd);

    for (i = TRACK_START;
         (i <= TRACK_END(di)) && sectors->nr_bytes;
         i += TRACK_STEP) {
        struct format_list *list = format_lists[i];
        if ((list == NULL) || (list->nr == 0))
            continue;
        if (list->nr > 1)
            errx(1, "T%u.%u: More than one format specified for IMG data",
                 TRACK_ARG(i));
        if (track_write_sectors(sectors, i, list->ent[0]) != 0)
            errx(1, "T%u.%u: %s: Unable to import IMG data",
                 TRACK_ARG(i),
                 disk_get_format_desc_name(list->ent[0]));
    }

    if (sectors->nr_bytes != 0)
        errx(1, "Unexpected extra data at end of IMG file");

    dump_track_list(d);

    disk_close(d);

    memfree(data);
    sectors->data = NULL;
    sectors->nr_bytes = 0;
    track_free_sector_buffer(sectors);
}

int main(int argc, char **argv)
{
    char in_suffix[8], out_suffix[8], *config = NULL, *format = NULL;
    int ch;

    const static char sopts[] = "hqviCp:P:r:s:e:S::Dkf:c:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "verbose", 0, NULL, 'v' },
        { "index-align", 0, NULL, 'i' },
        { "clear-bad-sectors", 0, NULL, 'C' },
        { "pll-period-adj", 1, NULL, 'p' },
        { "pll-phase-adj", 1, NULL, 'P' },
        { "rpm", 1, NULL, 'r' },
        { "start-cyl", 1, NULL, 's' },
        { "end-cyl", 1, NULL, 'e' },
        { "ss", 2, NULL, 'S' },
        { "double-step", 0, NULL, 'D' },
        { "kryoflux-hack", 0, NULL, 'k' },
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
        case 'C':
            clear_bad_sectors = 1;
            break;
        case 'p':
            pll_period_adj_pct = atoi(optarg);
            if ((pll_period_adj_pct < 0) || (pll_period_adj_pct > 100)) {
                warnx("Bad --pll-period-adj value '%s'", optarg);
                usage(1);
            }
            break;
        case 'P':
            pll_phase_adj_pct = atoi(optarg);
            if ((pll_phase_adj_pct < 0) || (pll_phase_adj_pct > 100)) {
                warnx("Bad --pll-phase-adj value '%s'", optarg);
                usage(1);
            }
            break;
        case 'r': {
            char *p;
            drive_rpm = strtol(optarg, &p, 10);
            data_rpm = (*p++ == ':') ? strtol(p, &p, 10) : drive_rpm;
            if ((drive_rpm < 100) || (drive_rpm > 500)
                || (data_rpm < 100) || (data_rpm > 500)) {
                warnx("Bad RPM value '%s'", optarg);
                usage(1);
            }
            break;
        }
        case 's':
            start_cyl = atoi(optarg);
            break;
        case 'e':
            end_cyl = atoi(optarg);
            break;
        case 'S':
            single_sided = optarg ? atoi(optarg) : 0;
            if ((single_sided < 0) || (single_sided > 1)) {
                warnx("Bad side specifier '%s'", optarg);
                usage(1);
            }
            break;
        case 'D':
            double_step = 1;
            break;
        case 'k':
            disk_flags |= DISKFL_kryoflux_hack;
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

    filename_extension(in, in_suffix, sizeof(in_suffix));
    filename_extension(out, out_suffix, sizeof(out_suffix));

    /* Pick a sane default format for certain sector image formats. */
    if (!format) {
        if (!strcmp(in_suffix, "imd") || !strcmp(out_suffix, "imd"))
            format = "ibm";
        else if (!strcmp(out_suffix, "adf"))
            format = "amigados";
        else if (!strcmp(out_suffix, "st"))
            format = "atari_st";
    }

    if (format && !strcmp(format, "probe_all")) {

        /* Lists all wholly- and partially-matching formats. */
        probe_stream();

    } else {

        format_lists = parse_config(config, format);

        if (!strcmp(in_suffix, "img") || !strcmp(in_suffix, "st"))
            handle_img();
        else
            handle_stream();

    }

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
