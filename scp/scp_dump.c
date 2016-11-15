/*
 * scp_dump.c
 * 
 * Communicate with Supercard Pro hardware to create .scp images.
 * 
 * Written in 2014 by Keir Fraser
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include <libdisk/util.h>
#include "scp.h"

#if defined (__APPLE__)
/* FTDI VCP driver: http://www.ftdichip.com/Drivers/VCP.htm */
#define DEFAULT_SERDEVICE  "/dev/cu.usbserial-SCP-JIM"
#else
#define DEFAULT_SERDEVICE  "/dev/ttyUSB0"
#endif

#define DEFAULT_UNIT       0
#define DEFAULT_STARTTRK   0
#define DEFAULT_ENDTRK     163
#define DEFAULT_REVS       2
static int double_step = 0;

static struct scp_params scp_params;

#define log(_f, _a...) do { if (!quiet) printf(_f, ##_a); } while (0)

static void usage(int rc)
{
    printf("Usage: scp_dump [options] out_file\n");
    printf("Options:\n");
    printf("  -h, --help        Display this information\n");
    printf("  -q, --quiet       Quiesce normal informational output\n");
    printf("  -d, --device      Name of serial device (%s)\n",
           DEFAULT_SERDEVICE);
    printf("  -u, --unit={A,B}  Which drive to dump (%c)\n",
           DEFAULT_UNIT ? 'B' : 'A');
    printf("  -r, --revs        Nr revolutions per track (%d)\n",
           DEFAULT_REVS);
    printf("  -R, --ramtest     Test SCP on-board SRAM before dumping\n");
    printf("  -s, --start       First track to dump (%d)\n",
           DEFAULT_STARTTRK);
    printf("  -e, --end         Last track to dump (%d)\n",
           DEFAULT_ENDTRK);
    printf("  -D, --double-step Double-step heads "
           "(40-cyl disk, 80-cyl drive)\n");
    printf("  -k, --step-delay  Delay between head steps, millisecs (%u)\n",
           default_scp_params.step_delay_ms);
    printf("  -K, --settle-delay  Settle time after seek, millisecs (%u)\n",
           default_scp_params.seek_settle_delay_ms);

    exit(rc);
}

/* When double-stepping, default cylinder numbers are halved. */
static unsigned int default_tracknr(unsigned int tracknr)
{
    unsigned int cyl = tracknr >> 1;
    unsigned int hd = tracknr & 1;
    if (double_step)
        cyl /= 2;
    return (cyl << 1) | hd;
}

int main(int argc, char **argv)
{
    struct scp_handle *scp;
    struct scp_flux flux;
    struct disk_header dhdr;
    struct track_header thdr;
    int rev, nr_revs = DEFAULT_REVS;
    int trk, start_trk = -1, end_trk = -1;
    unsigned int sizeof_thdr, unit = DEFAULT_UNIT;
    uint32_t *th_offs, file_off, dat_off;
    int ch, fd, quiet = 0, ramtest = 0;
    char *sername = DEFAULT_SERDEVICE;

    const static char sopts[] = "hqd:u:r:Rs:e:Dk:K:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "device", 1, NULL, 'd' },
        { "unit", 1, NULL, 'u' },
        { "revs", 1, NULL, 'r' },
        { "ramtest", 0, NULL, 'R' },
        { "start", 1, NULL, 's' },
        { "end", 1, NULL, 'e' },
        { "double-step", 0, NULL, 'D' },
        { "step-delay", 1, NULL, 'k' },
        { "settle-delay", 1, NULL, 'K' },
        { 0, 0, 0, 0 }
    };

    scp_params = default_scp_params;

    while ((ch = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (ch) {
        case 'h':
            usage(0);
            break;
        case 'q':
            quiet = 1;
            break;
        case 'd':
            sername = optarg;
            break;
        case 'u':
            if (strlen(optarg) != 1)
                goto bad;
            switch (*optarg) {
            case 'a': case 'A': unit = 0; break;
            case 'b': case 'B': unit = 1; break;
            default: bad:
                warnx("Bad drive unit '%s'", optarg);
                usage(1);
            }
            break;
        case 'r':
            nr_revs = atoi(optarg);
            break;
        case 'R':
            ramtest = 1;
            break;
        case 's':
            start_trk = atoi(optarg);
            break;
        case 'e':
            end_trk = atoi(optarg);
            break;
        case 'D':
            double_step = 1;
            break;
        case 'k':
            scp_params.step_delay_ms = atoi(optarg);
            break;
        case 'K':
            scp_params.seek_settle_delay_ms = atoi(optarg);
            break;
        default:
            usage(1);
            break;
        }
    }

    if (start_trk < 0)
        start_trk = default_tracknr(DEFAULT_STARTTRK);
    if (end_trk < 0)
        end_trk = default_tracknr(DEFAULT_ENDTRK);

    if (argc != (optind + 1))
        usage(1);

    if ((end_trk >= SCP_MAX_TRACKS) || (start_trk > end_trk)) {
        warnx("Bad track range (%u-%u)", start_trk, end_trk);
        usage(1);
    }

    if (nr_revs > ARRAY_SIZE(flux.info)) {
        warnx("Too many revolutions specified (%u, max %u)",
              nr_revs, (unsigned int)ARRAY_SIZE(flux.info));
        usage(1);
    }

    if ((fd = file_open(argv[optind], O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
        err(1, "Error creating %s", argv[optind]);

    memset(&dhdr, 0, sizeof(dhdr));
    memcpy(dhdr.sig, "SCP", sizeof(dhdr.sig));
    dhdr.version = 0x10; /* taken from existing images */
    dhdr.disk_type = DISKTYPE_amiga;
    dhdr.nr_revolutions = nr_revs;
    dhdr.start_track = start_trk;
    dhdr.end_track = end_trk;
    dhdr.flags = (1u<<_FLAG_writable); /* avoids need for checksum */
    write_exact(fd, &dhdr, sizeof(dhdr));

    th_offs = memalloc(SCP_MAX_TRACKS * sizeof(uint32_t));
    write_exact(fd, th_offs, SCP_MAX_TRACKS * sizeof(uint32_t));
    file_off = sizeof(dhdr) + SCP_MAX_TRACKS * sizeof(uint32_t);

    scp = scp_open(sername);
    if (!quiet)
        scp_printinfo(scp);
    if (ramtest)
        scp_ramtest(scp);
    scp_set_params(scp, &scp_params);
    scp_selectdrive(scp, unit);

    log("Reading track ");

    sizeof_thdr = 4 + 12*nr_revs;
    for (trk = start_trk; trk <= end_trk; trk++) {
        log("%-4u...", trk);
        fflush(stdout);

        scp_seek_track(scp, trk, double_step);
        scp_read_flux(scp, nr_revs, &flux);

        th_offs[trk] = htole32(file_off);

        memset(&thdr, 0, sizeof_thdr);
        memcpy(thdr.sig, "TRK", sizeof(thdr.sig));
        thdr.tracknr = trk;

        dat_off = sizeof_thdr;
        for (rev = 0; rev < nr_revs; rev++) {
            thdr.rev[rev].duration = htole32(flux.info[rev].index_time);
            thdr.rev[rev].nr_samples = htole32(flux.info[rev].nr_bitcells);
            thdr.rev[rev].offset = htole32(dat_off);
            dat_off += flux.info[rev].nr_bitcells * sizeof(uint16_t);
        }
        write_exact(fd, &thdr, sizeof_thdr);
        write_exact(fd, flux.flux, dat_off - sizeof_thdr);
        file_off += dat_off;

        log("\b\b\b\b\b\b\b");
    }

    log("\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

    scp_deselectdrive(scp, unit);
    scp_close(scp);

    lseek(fd, sizeof(dhdr), SEEK_SET);
    write_exact(fd, th_offs, SCP_MAX_TRACKS * sizeof(uint32_t));

    return 0;
}
