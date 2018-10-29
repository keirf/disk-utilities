/*
 * adfread.c
 * 
 * Read Amiga Disk File (ADF) images and write contents to a local directory
 * in the host environment.
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
#include <ctype.h>
#include <libdisk/util.h>

/* Physical characteristics of an AmigaDOS DS/DD floppy disk. */
#define BYTES_PER_BLOCK   512
#define BLOCKS_PER_TRACK   11
#define TRACKS_PER_CYL      2
#define CYLS_PER_DISK      80

/* Computed characteristics. */
#define TRACKS_PER_DISK   (TRACKS_PER_CYL*CYLS_PER_DISK)
#define BLOCKS_PER_DISK   (BLOCKS_PER_TRACK*TRACKS_PER_DISK)
#define BYTES_PER_DISK    (BYTES_PER_BLOCK*BLOCKS_PER_DISK)

#define HASH_SIZE (BYTES_PER_BLOCK/4-56)

typedef struct {
    uint8_t len;
    uint8_t chars[1];
} bcpl_string_t;

struct ffs_datestamp {
    uint32_t days, mins, ticks;
};

struct ffs_root_block {
    uint32_t type;
    uint32_t header_key;
    uint32_t max_seq;
    uint32_t hash_size;
    uint32_t mbz_0;
    uint32_t checksum;
    uint32_t hash[HASH_SIZE];
    uint32_t bitmap_flag;
    uint32_t bitmap_keys[25];
    uint32_t bitmap_extended;
    struct ffs_datestamp root_altered_datestamp;
    uint8_t disk_name[40];
    struct ffs_datestamp disk_altered_datestamp;
    struct ffs_datestamp disk_made_datestamp;
    uint32_t mbz_1[3];
    uint32_t subtype;
};

struct ffs_dir {
    uint32_t type;
    uint32_t header_key;
    uint32_t mbz_0[3];
    uint32_t checksum;
    uint32_t hash[HASH_SIZE];
    uint32_t mbz_1[2];
    uint32_t protection_bits;
    uint32_t mbz_2[1];
    uint8_t dir_comment[92];
    struct ffs_datestamp datestamp;
    uint8_t dir_name[36];
    uint32_t mbz_3[7];
    uint32_t hash_chain;
    uint32_t parent;
    uint32_t mbz_4[1];
    uint32_t subtype;
};

struct ffs_fileheader {
    uint32_t type;
    uint32_t header_key;
    uint32_t max_seq;
    uint32_t mbz_0[1];
    uint32_t first_data;
    uint32_t checksum;
    uint32_t data[HASH_SIZE];
    uint32_t mbz_1[2];
    uint32_t protection_bits;
    uint32_t file_size;
    uint8_t dir_comment[92];
    struct ffs_datestamp datestamp;
    uint8_t file_name[36];
    uint32_t mbz_3[7];
    uint32_t hash_chain;
    uint32_t parent;
    uint32_t extension;
    uint32_t subtype;
};

#define T_HEADER     2
#define T_LIST      16

#define ST_ROOT      1
#define ST_USERDIR   2
#define ST_FILE     -3

static int is_ffs, is_readonly;

/* read_exact, write_exact */
#include "../libdisk/util.c"

static void *get_block(int fd, unsigned int block)
{
    off_t off;
    void *dat;

    if (block >= BLOCKS_PER_DISK)
        errx(1, "Block index %u out of range", block);

    dat = malloc(BYTES_PER_BLOCK);
    if (dat == NULL)
        err(1, NULL);

    off = lseek(fd, block * BYTES_PER_BLOCK, SEEK_SET);
    if (off < 0)
        err(1, NULL);

    read_exact(fd, dat, BYTES_PER_BLOCK);

    return dat;
}

static void put_block(void *dat)
{
    memset(dat, 0xAA, BYTES_PER_BLOCK);
    free(dat);
}

static void checksum_block(void *dat)
{
    uint32_t sum = 0, *blk = dat;
    unsigned int i;

    for (i = 0; i < BYTES_PER_BLOCK/4; i++)
        sum += be32toh(blk[i]);

    if (sum != 0)
        errx(1, "Bad block checksum %08x", sum);
}

static const char *format_bcpl_string(uint8_t *bcpl_str)
{
    static char str[64];
    if (bcpl_str[0] > (sizeof(str)-1))
        errx(1, "BCPL string too long");
    sprintf(str, "%.*s", bcpl_str[0], &bcpl_str[1]);
    return str;
}

static time_t time_from_datestamp(struct ffs_datestamp *stamp)
{
    time_t time = (time_t)(8*365+2)*24*60*60;
    time += (time_t)be32toh(stamp->days)*24*60*60;
    time += (time_t)be32toh(stamp->mins)*60;
    time += (time_t)be32toh(stamp->ticks)/50;
    return time;
}

static const char *format_datestamp(struct ffs_datestamp *stamp)
{
    time_t time = time_from_datestamp(stamp);
    char *str = ctime(&time);
    str[strlen(str)-1] = '\0'; /* nobble the newline */
    return str;
}

static void set_times(const char *path, time_t time)
{
    struct utimbuf utimbuf = { time, time };
    (void)utime(path, &utimbuf);
}

static void handle_file(int fd, char *path, struct ffs_fileheader *file)
{
    int file_fd;
    unsigned int todo, nxtblk, data_per_block;
    time_t time = time_from_datestamp(&file->datestamp);

    printf(" %-54s %6u %s\n",
           path,
           be32toh(file->file_size),
           format_datestamp(&file->datestamp));

    if (is_readonly)
        return;

    file_fd = file_open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if (file_fd == -1)
        err(1, "%s", path);

    data_per_block = is_ffs ? BYTES_PER_BLOCK : BYTES_PER_BLOCK-24;

    todo = be32toh(file->file_size);
    for (nxtblk = 0; todo != 0; nxtblk++) {
        unsigned int idx, this_todo;
        char *dat;
        if (nxtblk == HASH_SIZE) {
            idx = be32toh(file->extension);
            put_block(file);
            file = get_block(fd, idx);
            checksum_block(file);
            if ((be32toh(file->type) != T_LIST) ||
                (be32toh(file->subtype) != ST_FILE))
                errx(1, "Bad file-ext block");
            nxtblk = 0;
        }
        idx = be32toh(file->data[HASH_SIZE-nxtblk-1]);
        dat = get_block(fd, idx);
        if (!is_ffs)
            checksum_block(dat);
        this_todo = (todo > data_per_block) ? data_per_block : todo;
        write_exact(file_fd, &dat[is_ffs?0:24], this_todo);
        put_block(dat);
        todo -= this_todo;
    }

    close(file_fd);
    set_times(path, time);

    put_block(file);
    free(path);
}

static void handle_dir(int fd, char *prefix, struct ffs_dir *dir)
{
    uint32_t idx;
    unsigned int i;
    char *path;
    const char *name;
    struct ffs_fileheader *file;

    if (!is_readonly)
        (void)posix_mkdir(prefix, 0777);

    strcat(prefix, "/");
    printf(" %-61s %s\n", prefix, format_datestamp(&dir->datestamp));

    for (i = 0; i < HASH_SIZE; i++) {
        idx = be32toh(dir->hash[i]);
        while (idx != 0) {
            file = get_block(fd, idx);
            if (be32toh(file->type) != T_HEADER)
                errx(1, "Not a header block (type %08x)", be32toh(file->type));
            checksum_block(file);

            name = format_bcpl_string(file->file_name);
            if ((path = malloc(strlen(prefix) + strlen(name) + 2)) == NULL)
                err(1, NULL);
            strcpy(path, prefix);
            strcat(path, name);

            idx = be32toh(file->hash_chain);

            switch ((int)be32toh(file->subtype)) {
            case ST_USERDIR:
                handle_dir(fd, path, (struct ffs_dir *)file);
                break;
            case ST_FILE:
                handle_file(fd, path, file);
                break;
            default:
                errx(1, "Unrecognised subtype %08x", be32toh(dir->subtype));
            }
        }
    }

    if (!is_readonly)
        set_times(prefix, time_from_datestamp(&dir->datestamp));

    put_block(dir);
    free(prefix);
}

int main(int argc, char **argv)
{
    int fd;
    off_t sz;
    struct ffs_root_block *root_block;
    char *boot_block, *dest_dir = ".", *tmp;
    const char *vol;

    if (argc == 3)
        dest_dir = argv[2];
    else if (argc == 2)
        is_readonly = 1;
    else
        errx(1, "Usage: adfread <filename> [<dest_dir>]");

    fd = file_open(argv[1], O_RDONLY);
    if (fd == -1)
        err(1, "%s", argv[1]);

    sz = lseek(fd, 0, SEEK_END);
    if (sz < 0)
        err(1, NULL);
    if (sz != BYTES_PER_DISK)
        errx(1, "Bad file size %ld bytes (expected %ld bytes)",
             (long)sz, (long)BYTES_PER_DISK);

    boot_block = get_block(fd, 0);
    if (strncmp(boot_block, "DOS", 3))
        errx(1, "Bad Amiga bootblock");
    is_ffs = boot_block[3] & 1;
    put_block(boot_block);

    root_block = get_block(fd, BLOCKS_PER_DISK/2);
    checksum_block(root_block);
    if ((be32toh(root_block->type) != T_HEADER) ||
        (be32toh(root_block->subtype) != ST_ROOT) ||
        (be32toh(root_block->hash_size) != HASH_SIZE))
        errx(1, "Bad root block");

    vol = format_bcpl_string(root_block->disk_name);
    if ((tmp = malloc(strlen(dest_dir) + 1 + strlen(vol) + 2)) == NULL)
        err(1, NULL);
    strcpy(tmp, dest_dir);
    dest_dir = tmp;
    if (dest_dir[strlen(dest_dir)-1] != '/')
        strcat(dest_dir, "/");
    strcat(dest_dir, vol);

    printf("%s is an %s volume\n", vol, is_ffs ? "FFS" : "OFS");
    printf("Created:\t%s\n", 
           format_datestamp(&root_block->disk_made_datestamp));
    printf("Last altered:\t%s\n",
           format_datestamp(&root_block->disk_altered_datestamp));

    handle_dir(fd, dest_dir, (struct ffs_dir *)root_block);

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
