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

#define DEFAULT_SERDEVICE  "/dev/ttyUSB0"
#define DEFAULT_NRTRACKS   164

#define log(_f, _a...) do { if (!quiet) printf(_f, ##_a); } while (0)

struct track_header {
    uint8_t sig[3];
    uint8_t tracknr;
    struct {
        uint32_t duration;
        uint32_t nr_samples;
        uint32_t offset;
    } rev[5];
};

static void usage(int rc)
{
    printf("Usage: scp_dump [options] out_file\n");
    printf("Options:\n");
    printf("  -h, --help    Display this information\n");
    printf("  -q, --quiet   Quiesce normal informational output\n");
    printf("  -d, --device  Name of serial device (%s)\n", DEFAULT_SERDEVICE);
    printf("  -r, --ramtest Test SCP on-board SRAM before dumping\n");
    printf("  -t, --tracks  Nr tracks to dump (%d)\n", DEFAULT_NRTRACKS);

    exit(rc);
}

int main(int argc, char **argv)
{
    struct scp_handle *scp;
    struct scp_flux flux;
    struct disk_header dhdr;
    struct track_header thdr;
    unsigned int rev, trk, nr_tracks = DEFAULT_NRTRACKS;
    uint32_t *th_offs, file_off, dat_off;
    int ch, fd, quiet = 0, ramtest = 0;
    char *sername = DEFAULT_SERDEVICE;

    const static char sopts[] = "hqd:rt:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "device", 1, NULL, 'd' },
        { "ramtest", 0, NULL, 'r' },
        { "tracks", 1, NULL, 't' },
        { 0, 0, 0, 0 }
    };

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
        case 'r':
            ramtest = 1;
            break;
        case 't':
            nr_tracks = atoi(optarg);
            break;
        default:
            usage(1);
            break;
        }
    }

    if (argc != (optind + 1))
        usage(1);

    if (nr_tracks > 168) {
        warnx("Too many tracks specified (%u)", nr_tracks);
        usage(1);
    }

    if ((fd = file_open(argv[1], O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
        err(1, "Error creating %s", argv[1]);

    memset(&dhdr, 0, sizeof(dhdr));
    memcpy(dhdr.sig, "SCP", sizeof(dhdr.sig));
    dhdr.version = 0x10; /* taken from existing images */
    dhdr.disk_type = DISKTYPE_amiga;
    dhdr.nr_revolutions = 5;
    dhdr.end_track = nr_tracks - 1;
    dhdr.flags = (1u<<_FLAG_writable); /* avoids need for checksum */
    write_exact(fd, &dhdr, sizeof(dhdr));

    th_offs = memalloc(nr_tracks * sizeof(uint32_t));
    write_exact(fd, th_offs, nr_tracks * sizeof(uint32_t));
    file_off = sizeof(dhdr) + nr_tracks * sizeof(uint32_t);

    scp = scp_open(sername);
    if (!quiet)
        scp_printinfo(scp);
    if (ramtest)
        scp_ramtest(scp);
    scp_selectdrive(scp, 0);

    log("Reading track ");

    for (trk = 0; trk < nr_tracks; trk++) {
        log("%-4u...", trk);
        fflush(stdout);

        scp_seek_track(scp, trk);
        scp_read_flux(scp, &flux);

        th_offs[trk] = htole32(file_off);

        memset(&thdr, 0, sizeof(thdr));
        memcpy(thdr.sig, "TRK", sizeof(thdr.sig));
        thdr.tracknr = trk;

        dat_off = sizeof(thdr);
        for (rev = 0; rev < ARRAY_SIZE(thdr.rev); rev++) {
            thdr.rev[rev].duration = htole32(flux.info[rev].index_time);
            thdr.rev[rev].nr_samples = htole32(flux.info[rev].nr_bitcells);
            thdr.rev[rev].offset = htole32(dat_off);
            dat_off += flux.info[rev].nr_bitcells * sizeof(uint16_t);
        }
        write_exact(fd, &thdr, sizeof(thdr));
        write_exact(fd, flux.flux, dat_off - sizeof(thdr));
        file_off += dat_off;

        log("\b\b\b\b\b\b\b");
    }

    log("\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

    scp_deselectdrive(scp, 0);
    scp_close(scp);

    lseek(fd, sizeof(dhdr), SEEK_SET);
    write_exact(fd, th_offs, nr_tracks * sizeof(uint32_t));

    return 0;
}
