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

#define offsetof(a,b) __builtin_offsetof(a,b)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ABS(_x) ({                              \
    typeof(_x) __x = (_x);                      \
    (__x < 0) ? -__x : __x;                     \
})

/* Physical DS/DD disk properties. */
#define TRACKS_PER_CYL           2
#define CYLS_PER_DISK           80
#define TRACKS_PER_DISK   (TRACKS_PER_CYL*CYLS_PER_DISK)

/* AmigaDOS logical format properties. */
#define ADOS_BYTES_PER_BLOCK   512
#define ADOS_BLOCKS_PER_TRACK   11

struct ados_blk {
    uint8_t  format, track, sector, sectors_to_gap;
    uint8_t  lbl[16];
    uint32_t hdr_checksum;
    uint32_t dat_checksum;
    uint8_t  dat[ADOS_BYTES_PER_BLOCK];
};

static int is_readonly;
static unsigned int bytes_per_track, mfm_bytes_per_track;

static void read_exact(int fd, void *buf, size_t count)
{
    size_t done;

    while ( count > 0 )
    {
        done = read(fd, buf, count);
        if ( (done < 0) && ((errno == EAGAIN) || (errno == EINTR)) )
            done = 0;
        if ( done < 0 )
            err(1, NULL);
        count -= done;
    }
}

static void write_exact(int fd, const void *buf, size_t count)
{
    size_t done;

    while ( count > 0 )
    {
        done = write(fd, buf, count);
        if ( (done < 0) && ((errno == EAGAIN) || (errno == EINTR)) )
            done = 0;
        if ( done < 0 )
            err(1, NULL);
        count -= done;
    }
}

static void *get_track(int fd, unsigned int trkidx)
{
    off_t off;
    void *dat;

    if ( trkidx >= TRACKS_PER_DISK )
        errx(1, "Track index %u out of range", trkidx);

    dat = malloc(bytes_per_track);
    if ( dat == NULL )
        err(1, NULL);

    off = lseek(fd, trkidx * bytes_per_track, SEEK_SET);
    if ( off < 0 )
        err(1, NULL);

    read_exact(fd, dat, bytes_per_track);

    return dat;
}

static void put_track(void *dat)
{
    memset(dat, 0xAA, bytes_per_track);
    free(dat);
}

/* MFM and latency-info bytes are interleaved in the raw data.*/
#define latbyte(trk, pos) ((trk)[(pos)*2])
#define mfmbyte(trk, pos) ((trk)[(pos)*2+1])

struct stream {
    unsigned char *pdat;
    unsigned int bitoff;
    uint16_t word;
};

static void stream_nextbit(struct stream *s)
{
    s->word <<= 1;
    if ( *s->pdat & (0x80u >> s->bitoff) )
        s->word |= 1;

    if ( ++s->bitoff == 8 )
    {
        s->bitoff = 0;
        s->pdat += 2;
    }
}

static void stream_nextbits(struct stream *s, unsigned int count)
{
    unsigned int i;
    for ( i = 0; i < count; i++ )
        stream_nextbit(s);
}

static void stream_init(
    struct stream *s, unsigned char *dat, unsigned int bitoff)
{
    s->pdat = &mfmbyte(dat, bitoff/8);
    s->bitoff = bitoff % 8;
    stream_nextbits(s, 16);
}

static uint32_t mfm_decode(void *dat, unsigned int len)
{
    uint32_t *odd = dat, *even = odd + len/4, csum = 0;
    unsigned int i;

    if ( len & 3 )
        errx(1, "Internal error: odd # MFM bytes to decode (%u)\n", len);

    for ( i = 0; i < len/4; i++, odd++, even++ )
    {
        csum ^= *odd ^ *even;
        *odd = ((*odd & 0x55555555u) << 1) | (*even & 0x55555555u);
    }

    return csum & 0x55555555;
}

static void *parse_ados_track(void *dat, unsigned int tracknr)
{
    struct stream s;
    char *ados_blocks;
    unsigned int i, j, valid_blocks = 0, labelled_blocks = 0;

    ados_blocks = malloc(ADOS_BYTES_PER_BLOCK * ADOS_BLOCKS_PER_TRACK);
    if ( ados_blocks == NULL )
        err(1, NULL);
    for ( i = 0; i < ADOS_BYTES_PER_BLOCK * ADOS_BLOCKS_PER_TRACK / 4; i++ )
        memcpy((uint32_t *)ados_blocks + i, "NDOS", 4);

    stream_init(&s, dat, 0);
    for ( i = 16;
          (i < (mfm_bytes_per_track-2048)*8) &&
              (valid_blocks != ((1u<<ADOS_BLOCKS_PER_TRACK)-1));
          i++, stream_nextbit(&s) )
    {
        struct stream ados_stream;
        struct ados_blk ados_blk;
        char raw_mfm_dat[2*sizeof(struct ados_blk)];
        uint32_t csum;

        if ( s.word != 0x4489 )
            continue;

        ados_stream = s;
        stream_nextbits(&ados_stream, 16);
        if ( ados_stream.word != 0x4489 )
            continue;

        for ( j = 0; j < sizeof(raw_mfm_dat)/2; j++ )
        {
            stream_nextbits(&ados_stream, 16);
            ((uint16_t *)raw_mfm_dat)[j] = htons(ados_stream.word);
        }

        csum = mfm_decode(&raw_mfm_dat[2*0], 4);
        memcpy(&ados_blk, &raw_mfm_dat[0], 4);
        csum ^= mfm_decode(&raw_mfm_dat[2*4], 16);
        memcpy(ados_blk.lbl, &raw_mfm_dat[2*4], 16);
        csum ^= mfm_decode(&raw_mfm_dat[2*20], 4);
        ados_blk.hdr_checksum = ((uint32_t *)raw_mfm_dat)[2*20/4];
        if ( csum != 0 )
            continue;

        csum = mfm_decode(&raw_mfm_dat[2*24], 4);
        ados_blk.dat_checksum = ((uint32_t *)raw_mfm_dat)[2*24/4];
        csum ^= mfm_decode(&raw_mfm_dat[2*28], ADOS_BYTES_PER_BLOCK);
        if ( csum != 0 )
            continue;

        if ( (ados_blk.format != 0xffu) ||
             (ados_blk.track != tracknr) ||
             (ados_blk.sector >= ADOS_BLOCKS_PER_TRACK) ||
             (valid_blocks & (1u<<ados_blk.sector)) )
            continue;

        for ( j = 0; j < 16; j++ )
            if ( ados_blk.lbl[j] != 0 )
                labelled_blocks |= 1u << ados_blk.sector;

        memcpy(&ados_blocks[ados_blk.sector*ADOS_BYTES_PER_BLOCK],
               &raw_mfm_dat[2*28],
               ADOS_BYTES_PER_BLOCK);
        valid_blocks |= 1u << ados_blk.sector;
    }

    if ( (valid_blocks == ((1u<<ADOS_BLOCKS_PER_TRACK)-1)) &&
         (labelled_blocks == 0) )
        goto out;

    printf("Track %u: ", tracknr);

    if ( valid_blocks != ((1u<<ADOS_BLOCKS_PER_TRACK)-1) )
    {
        unsigned int bad = 0;
        for ( i = 0; i < ADOS_BLOCKS_PER_TRACK; i++ )
            if ( !(valid_blocks & (1u<<i)) )
                bad++;
        printf("[%u missing ADOS sectors] ", bad);
    }

    if ( labelled_blocks != 0 )
    {
        unsigned int bad = 0;
        for ( i = 0; i < ADOS_BLOCKS_PER_TRACK; i++ )
            if ( (labelled_blocks & (1u<<i)) )
                bad++;
        printf("[%u non-empty sector labels ]", bad);
    }

    printf("\n");

out:
    return ados_blocks;
}

int main(int argc, char **argv)
{
    int in_fd, out_fd;
    off_t sz;
    unsigned int i;
    char *destname;
    void *track, *ados_blocks;

    if ( argc == 3 )
        destname = argv[2];
    else if ( argc == 2 )
        is_readonly = 1;
    else
        errx(1, "Usage: mfmparse <in> [<out>]");

    in_fd = open(argv[1], O_RDONLY);
    if ( in_fd == -1 )
        err(1, "%s", argv[1]);
    if ( !is_readonly )
    {
        out_fd = open(destname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if ( out_fd == -1 )
            err(1, "%s", destname);
    }

    sz = lseek(in_fd, 0, SEEK_END);
    if ( sz < 0 )
        err(1, NULL);
    if ( sz % (TRACKS_PER_DISK*2) )
        errx(1, "Weird file size indivisible by number of tracks.\n");
    bytes_per_track = sz / TRACKS_PER_DISK;
    mfm_bytes_per_track = bytes_per_track / 2;
    printf("Found %u bytes per track in %u tracks.\n",
           bytes_per_track, TRACKS_PER_DISK);

    for ( i = 0; i < 160; i++ )
    {
        track = get_track(in_fd, i);
        ados_blocks = parse_ados_track(track, i);
        put_track(track);
        if ( !is_readonly )
            write_exact(out_fd, ados_blocks,
                        ADOS_BYTES_PER_BLOCK*ADOS_BLOCKS_PER_TRACK);
        free(ados_blocks);
    }

    return 0;
}
