/*
 * scp_write.c
 * 
 * Communicate with Supercard Pro hardware to write .scp images to disk.
 * 
 * Written in 2014-2015 by Keir Fraser
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
#define DEFAULT_STARTTRK   0
#define DEFAULT_ENDTRK     163

#define log(_f, _a...) do { if (!quiet) printf(_f, ##_a); } while (0)

struct track_header {
    uint8_t sig[3];
    uint8_t tracknr;
    uint32_t duration;
    uint32_t nr_samples;
    uint32_t offset;
};

static void usage(int rc)
{
    printf("Usage: scp_write [options] out_file\n");
    printf("Options:\n");
    printf("  -h, --help    Display this information\n");
    printf("  -q, --quiet   Quiesce normal informational output\n");
    printf("  -d, --device  Name of serial device (%s)\n", DEFAULT_SERDEVICE);
    printf("  -s, --start   First track to write (%d)\n", DEFAULT_STARTTRK);
    printf("  -e, --end     Last track to write (%d)\n", DEFAULT_ENDTRK);

    exit(rc);
}

int main(int argc, char **argv)
{
    struct scp_handle *scp;
    struct disk_header dhdr;
    struct track_header thdr;
    struct scp_flux flux;
    unsigned int trk, start_trk = DEFAULT_STARTTRK, end_trk = DEFAULT_ENDTRK;
    uint32_t *th_offs, th_off, dat_off, drvtime, imtime, i, j;
    uint16_t dat[256*1024/2], odat[256*1024/2];
    int ch, fd, quiet = 0;
    char *sername = DEFAULT_SERDEVICE;

    const static char sopts[] = "hqd:s:e:";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "quiet", 0, NULL, 'q' },
        { "device", 1, NULL, 'd' },
        { "start", 1, NULL, 's' },
        { "end", 1, NULL, 'e' },
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
        case 's':
            start_trk = atoi(optarg);
            break;
        case 'e':
            end_trk = atoi(optarg);
            break;
        default:
            usage(1);
            break;
        }
    }

    if (argc != (optind + 1))
        usage(1);

    if ((end_trk >= 168) || (start_trk > end_trk)) {
        warnx("Bad track range (%u-%u)", start_trk, end_trk);
        usage(1);
    }

    if ((fd = file_open(argv[optind], O_RDONLY)) == -1)
        err(1, "Error opening %s", argv[optind]);

    read_exact(fd, &dhdr, sizeof(dhdr));
    if (memcmp(dhdr.sig, "SCP", 3))
        errx(1, "%s: Not an SCP image", argv[optind]);

    th_offs = memalloc((end_trk+1) * sizeof(uint32_t));
    read_exact(fd, th_offs, (end_trk+1) * sizeof(uint32_t));

    scp = scp_open(sername);
    if (!quiet)
        scp_printinfo(scp);
    scp_selectdrive(scp, 0);

    scp_seek_track(scp, 0);
    scp_read_flux(scp, 1, &flux);
    drvtime = htole32(flux.info[0].index_time);
    log("Drive speed: %u us per revolution (%.2f RPM)\n",
        drvtime/40, 60000000.0/(drvtime/40));

    log("Writing track ");

    for (trk = start_trk; trk <= end_trk; trk++) {

        uint64_t x = 0, y;

        if ((trk < dhdr.start_track) || (trk > dhdr.end_track))
            continue;

        th_off = le32toh(th_offs[trk]);
        if (th_off == 0)
            continue;

        log("%-4u...", trk);
        fflush(stdout);

        lseek(fd, th_off, SEEK_SET);
        read_exact(fd, &thdr, sizeof(thdr));
        if (memcmp(thdr.sig, "TRK", 3) || thdr.tracknr != trk)
            errx(1, "%s: Track %u bad signature", argv[optind], trk);
        imtime = htole32(thdr.duration);

        dat_off = th_off + le32toh(thdr.offset);
        lseek(fd, dat_off, SEEK_SET);
        read_exact(fd, dat, le32toh(thdr.nr_samples) * 2);

        /* Resample data to match target drive speed */
        for (i = j = 0; i < le32toh(thdr.nr_samples); i++) {
            if (dat[i]) {
                x += (uint64_t)be16toh(dat[i]) * drvtime;
            } else {
                x += (uint64_t)0x10000u * drvtime;
                if (i < (le32toh(thdr.nr_samples)-1))
                    continue;
            }
            y = x / imtime;
            while (y >= 0x10000u) {
                odat[j++] = 0;
                y -= 0x10000u;
            }
            odat[j++] = htobe16(y ?: 1);
            x %= imtime; /* carry the fractional part */
        }

        scp_seek_track(scp, trk);
        scp_write_flux(scp, odat, j);

        log("\b\b\b\b\b\b\b");
    }

    log("\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

    scp_deselectdrive(scp, 0);
    scp_close(scp);

    return 0;
}
