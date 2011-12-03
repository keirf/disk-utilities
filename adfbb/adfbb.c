/******************************************************************************
 * adfbb.c
 * 
 * Check boot block in an Amiga Disk File (ADF) image and optionally fix.
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

/* read_exact, write_exact */
#include "../libdisk/util.c"

uint32_t checksum(void *dat)
{
    uint32_t csum = 0, *bb = dat;
    unsigned int i;
    for (i = 0; i < 1024/4; i++) {
        uint32_t x = ntohl(bb[i]);
        if ((csum + x) < csum)
            csum++;
        csum += x;
    }
    return ~csum;
}

static int compare_bb(char *bb, const char *tmpl, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++)
        if (bb[12+i] != tmpl[i])
            return -1;
    return 0;
}

static void copy_bb(char *bb, const char *tmpl, unsigned int sz)
{
    unsigned int i;
    memset(&bb[4], 0, 1020);
    for (i = 0; i < sz; i++)
        bb[12+i] = tmpl[i];
    *(uint32_t *)&bb[8] = htonl(880);
    *(uint32_t *)&bb[4] = htonl(checksum(bb));
}

static void decode_new_bb(char *bb, const char *filename)
{
    uint32_t *p, type, longs;
    void *buf;
    off_t sz;
    int fd;

    if ((fd = open(filename, O_RDONLY)) == -1)
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
        type = ntohl(*p++);
        longs = ntohl(*p++);
        if (type == 0x3e9)
            break;
        p += longs;
    }

    if ((type != 0x3e9) || ((char *)&p[longs] > ((char *)buf + sz)))
        errx(1, "No valid executable chunk detected");

    printf("Found valid %u-byte executable chunk\n", longs*4);
    if (longs > 0x100)
        errx(1, "Executable chunk too large (%u bytes)", longs*4);

    memset(bb, 0, 1024);
    memcpy(bb, p, longs*4);

    free(buf);
}

static int test_lamer(char *bb)
{
    unsigned int i;
    char sig[0x20];
    for (i = 0; i < (sizeof(sig)-1); i++)
        sig[i] = bb[0x37a+i] ^ bb[0x395];
    sig[sizeof(sig)-1] = '\0';
    return strcmp(sig, "The LAMER Exterminator !!!");
}

static const char kick13_bootable[] = {
    0x43, 0xfa, 0x00, 0x18,
    0x4e, 0xae, 0xff, 0xa0, 0x4a, 0x80, 0x67, 0x0a,
    0x20, 0x40, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00,
    0x4e, 0x75, 0x70, 0xff, 0x60, 0xfa, 0x64, 0x6f,
    0x73, 0x2e, 0x6c, 0x69, 0x62, 0x72, 0x61, 0x72,
    0x79, 0x00
};

static const char kick20_bootable[] = {
    0x43, 0xfa, 0x00, 0x3e,
    0x70, 0x25, 0x4e, 0xae, 0xfd, 0xd8, 0x4a, 0x80,
    0x67, 0x0c, 0x22, 0x40, 0x08, 0xe9, 0x00, 0x06,
    0x00, 0x22, 0x4e, 0xae, 0xfe, 0x62, 0x43, 0xfa,
    0x00, 0x18, 0x4e, 0xae, 0xff, 0xa0, 0x4a, 0x80,
    0x67, 0x0a, 0x20, 0x40, 0x20, 0x68, 0x00, 0x16,
    0x70, 0x00, 0x4e, 0x75, 0x70, 0xff, 0x4e, 0x75,
    0x64, 0x6f, 0x73, 0x2e, 0x6c, 0x69, 0x62, 0x72,
    0x61, 0x72, 0x79, 0x00, 0x65, 0x78, 0x70, 0x61,
    0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x2e, 0x6c, 0x69,
    0x62, 0x72, 0x61, 0x72, 0x79, 0x00
};

int main(int argc, char **argv)
{
    int fd, fixup = 0;
    char bb[1024];
    uint32_t rootblock, csum;

    if (argc == 3) {
        if (!strcmp(argv[2], "-w"))
            fixup = 1;
        else if (!strcmp(argv[2], "-f"))
            fixup = 2;
        else if (!strncmp(argv[2], "-g", 2))
            fixup = 3;
        else
            goto usage;
        argc--;
    }

    if (argc != 2) {
    usage:
        errx(1, "Usage: adfbb <filename> [-w] [-f] [-g<new block>]\n"
             " -w: Overwrite bootblock with Kick 1.3 block\n"
             " -f: Fix up bootblock checksum\n"
             " -g: New Amiga hunk file to decode and poke");
    }

    fd = open(argv[1], fixup ? O_RDWR : O_RDONLY);
    if (fd == -1)
        err(1, "%s", argv[1]);

    read_exact(fd, &bb, sizeof(bb));

    if (strncmp(&bb[0], "DOS", 3)) {
        printf("Volume type: NDOS\n");
    } else {
        uint8_t flags = bb[3];
        if (flags & 0xf8)
            printf("** Meaningless flags set at byte offset 3 (%02x)\n",
                   flags);
        printf("Volume type: %cFS ", (flags & 1) ? 'F' : 'O');
        if (flags & 2)
            printf("INTL ");
        if (flags & 4)
            printf("DIRC&INTL");
        printf("\n");
    }

    if ((csum = checksum(bb)) != 0) {
        printf("Disk is not bootable.\n");
        goto out;
    }

    rootblock = ntohl(*(uint32_t *)&bb[8]);
    if (rootblock != 880)
        printf("** Bogus rootblock index %u\n", rootblock);

    if (!compare_bb(bb, kick13_bootable, sizeof(kick13_bootable)))
        printf("Kickstart 1.3 bootblock\n");
    else if (!compare_bb(bb, kick20_bootable, sizeof(kick20_bootable)))
        printf("Kickstart 1.3 bootblock\n");
    else if (!test_lamer(bb))
        printf("** LAMER EXTERMINATOR VIRUS!!!!!! **\n");
    else
        printf("** Unrecognised bootable bootblock!\n");

out:
    if (fixup) {
        if (fixup == 1)
            copy_bb(bb, kick13_bootable, sizeof(kick13_bootable));
        else if (fixup == 3)
            decode_new_bb(bb, argv[2]+2);
        *(uint32_t *)&bb[4] = 0;
        *(uint32_t *)&bb[4] = htonl(checksum(bb));
        lseek(fd, 0, SEEK_SET);
        write_exact(fd, bb, 1024);
        printf("Bootblock fixed up.\n");
    }

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
