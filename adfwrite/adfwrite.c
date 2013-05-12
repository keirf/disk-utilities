/*
 * adfwrite.c
 * 
 * Stuff sectors of an ADF image with specified data.
 * 
 * Written in 2011 by Keir Fraser
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
#include <libdisk/util.h>

/* read_exact, write_exact */
#include "../libdisk/util.c"

static void *decode_dat(const char *filename, unsigned int *psz)
{
    uint32_t *p, type, longs;
    void *buf;
    off_t sz;
    int fd;

    if ((fd = file_open(filename, O_RDONLY)) == -1)
        err(1, "%s", filename);

    if ((sz = lseek(fd, 0, SEEK_END)) < 0)
        err(1, NULL);

    if ((buf = malloc(sz)) == NULL)
        err(1, NULL);
    lseek(fd, 0, SEEK_SET);
    read_exact(fd, buf, sz);
    close(fd);

    p = buf;
    type = longs = 0;
    while ((char *)p < ((char *)buf + sz - 8)) {
        type = be32toh(*p++);
        longs = be32toh(*p++);
        if (type == 0x3e9)
            break;
        p += longs;
    }

    if ((type != 0x3e9) || ((char *)&p[longs] > ((char *)buf + sz)))
        errx(1, "No valid executable chunk detected");

    *psz = longs * 4;
    printf("Found valid %u-byte executable chunk\n", *psz);

    return p;
}

uint32_t next_key(uint32_t w)
{
    static uint32_t x = 0x075bcd15;
    static uint32_t y = 0x159a55e5;
    static uint32_t z = 0x1f123bb5;
    uint32_t t;
 
    t = x ^ (x << 11);
    x = y; y = z; z = w;
    return w = w ^ (w >> 19) ^ (t ^ (t >> 8));
}

int main(int argc, char **argv)
{
    int fd, postfill = 0;
    off_t sz;
    char *dat, zero[512] = { 0 };
    unsigned int fsec, lsec, csec, datsz;
    uint32_t key = 0;

    while (argc > 5) {
        if (!strcmp(argv[argc-1], "-f"))
            postfill = 1;
        else if (!strncmp(argv[argc-1], "-e", 2))
            key = strtol(argv[argc-1]+2, NULL, 16);
        else
            goto usage;
        argc--;
    }

    if (argc != 5) {
    usage:
        errx(1, "Usage: adfwrite <adffile> <datfile> <startsec> "
             "<endsec> [-f] [-e<key>]\n"
             " <datfile> must be a valid Amiga hunk file\n"
             " <startsec>-<endsec> range is *inclusive* and *decimal*\n"
             " -f: Postfill up to <endsec> with zeroes\n"
             " -e: Encrypt with given hex key");
    }

    fd = file_open(argv[1], O_RDWR);
    if (fd == -1)
        err(1, "%s", argv[1]);

    fsec = atoi(argv[3]);
    lsec = atoi(argv[4]);
    if ((fsec < 2) || (lsec >= (160*11)) || (fsec > lsec))
        errx(1, "Bad sector range %u-%u", fsec, lsec);

    sz = lseek(fd, 0, SEEK_END);
    if (sz != (160*11*512))
        errx(1, "Bad ADF image size (%u bytes)", (unsigned int)sz);

    dat = decode_dat(argv[2], &datsz);
    if (datsz > ((lsec - fsec + 1) * 512))
        errx(1, "Data too big (%u bytes > %u bytes)",
             datsz, (lsec - fsec + 1) * 512);

    if (key) {
        unsigned int i;
        for (i = 0; i < datsz; i += 4) {
            key = next_key(key);
            *(uint32_t *)&dat[i] ^= htobe32(key);
        }
        for (i = 0; i < 512; i += 4) {
            key = next_key(key);
            *(uint32_t *)&zero[i] ^= key;
        }
    }

    lseek(fd, fsec*512, SEEK_SET);
    write_exact(fd, dat, datsz);
    if (datsz & 511)
        write_exact(fd, zero, 512 - (datsz&511));
    csec = fsec + (datsz+511)/512;

    if (postfill)
        for (; csec <= lsec; csec++)
            write_exact(fd, zero, 512);

    printf("Sectors %u-%u inclusive are stuffed!\n", fsec, csec - 1);

    close(fd);
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
