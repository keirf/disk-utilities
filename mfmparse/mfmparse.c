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

/* PAL Amiga CIA frequency 0.709379 MHz */
#define CIA_FREQ 709379u
#define CIA_NS_PER_TICK (1000000000u/CIA_FREQ)

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
    unsigned int latency;
    uint16_t word;
};

static void stream_nextbit(struct stream *s)
{
    s->word <<= 1;
    if ( *s->pdat & (0x80u >> s->bitoff) )
        s->word |= 1;

    if ( ++s->bitoff == 8 )
    {
        s->latency += (s->pdat[1] & 0x7f) * CIA_NS_PER_TICK;
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

#if 0
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
#endif

static uint16_t copylock_decode_word(uint32_t x)
{
    uint16_t y = 0;
    unsigned int i;
    for ( i = 0; i < 16; i++ )
    {
        y |= (x & 1) << i;
        x >>= 2;
    }
    return y;
}


/*
 * Format:
 *  11 sectors:
 *   [sync] [idx byte] [512 * random data bytes]
 *  Sector 6:
 *   First 16 bytes interrupt random stream with signature "Rob Northen Comp"
 *  Random data bytes:
 *   byte[n] = (byte[n-1] << 1) | (RND?1:0)
 *  MFM encoding:
 *   In place, no even/odd split.
 *  Sector gap:
 *   48 zero bytes will do
 *  Timings:
 *   Sync 0x8912 is 5% faster; Sync 0x8914 is 5% slower
 */
static void *parse_copylock_track(void *dat, unsigned int tracknr)
{
    struct stream s;
    int i, j, k=0, sync = 0;
    static const uint16_t sync_list[] = {
        0x8a91, 0x8a44, 0x8a45, 0x8a51, 0x8912, 0x8911,
        0x8914, 0x8915, 0x8944, 0x8945, 0x8951 };
    static const uint16_t sec6_sig[] = {
        0x526f, 0x6220, 0x4e6f, 0x7274, /* "Rob Northen Comp" */
        0x6865, 0x6e20, 0x436f, 0x6d70 };
    uint32_t latency[11];
    char *info;
    uint8_t key;

    if ( (info = malloc(ARRAY_SIZE(sync_list) * 24)) == NULL )
        err(1, NULL);

    stream_init(&s, dat, 0);
    for ( i = 16;
          (i < (mfm_bytes_per_track-15000)*8) &&
              (sync < ARRAY_SIZE(sync_list));
          i++, stream_nextbit(&s) )
    {
        uint16_t idx;
        if ( s.word != sync_list[sync] )
            continue;
        stream_nextbits(&s, 16);
        idx = copylock_decode_word(s.word);
        if ( sync != idx )
            continue;
        s.latency = 0;
        for ( j = 0; j < 256; j++ )
        {
            uint32_t x;
            stream_nextbits(&s, 16);
            x = s.word << 16;
            stream_nextbits(&s, 16);
            x |= s.word;
            x = copylock_decode_word(x);
            if ( (idx == 0) && (j == 0) )
                key = x>>9;
            if ( (idx == 6) && (j < ARRAY_SIZE(sec6_sig)) )
            {
                if ( x != sec6_sig[j] )
                {
                    printf("CopyLock signature fail\n");
                    goto fail;
                }
            }
            else
            {
                if ( (((x >> 7) ^ x) & 0xf8) ||
                     (((x>>9) ^ key) & 0x7f) )
                {
                    printf("CopyLock RNG fail %u %u\n", idx, j);
                    goto fail;
                }
                key = x;
            }
            if ( j < 12 )
            {
                *(uint16_t *)&info[k*2] = htons(x);
                k++;
            }
        }
        latency[idx] = s.latency;
#if 0
        for ( ; j < 275; j++ )
        {
            uint32_t x;
            stream_nextbits(&s, 16);
            x = s.word << 16;
            stream_nextbits(&s, 16);
            x |= s.word;
            x = copylock_decode_word(x);
            printf("%04x ", x);
        }
        printf("\n");
#endif

        sync++;
    }

fail:
    if ( sync != ARRAY_SIZE(sync_list) )
    {
        free(info);
        return NULL;
    }

    for ( i = 0; i < ARRAY_SIZE(latency); i++ )
    {
        float d = (100.0 * ((int)latency[i] - (int)latency[5])) / (int)latency[5];
        switch ( i )
        {
        case 4:
            if ( d > -4.8 )
                printf("WARNING: Short sector is only %.2f%% different\n", d);
            break;
        case 6:
            if ( d < 4.8 )
                printf("WARNING: Long sector is only %.2f%% different\n", d);
            break;
        default:
            if ( (d < -2.0) || (d > 2.0) )
                printf("WARNING: Normal sector is %.2f%% different\n", d);
            break;
        }
    }

    printf("Copylock detected!\n");
    for ( i = 0; i < 11*24; i++ )
    {
        printf("%02x ", (uint8_t)info[i]);
        if ( (i % 24) == 23 )
            printf("\n");
    }
    printf("\n");
    return info;
}

/*
 * Format:
 *  Track header:
 *  0x4489 0x552a 0xaaaa 
 *  6 sectors:
 *   [1 word checksum] [512 words data]
 *  Checksum is sum of all 512 data words
 *  MFM encoding is alternating even/odd words
 *  No sector gaps
 */
static void *parse_lemmings_track(void *dat, unsigned int tracknr)
{
    struct stream s;
    char *blocks;
    unsigned int i, j, k, valid_blocks = 0, bad;

    blocks = malloc(1024 * 6);
    if ( blocks == NULL )
        err(1, NULL);
    for ( i = 0; i < 6 * 1024 / 4; i++ )
        memcpy((uint32_t *)blocks + i, "NDOS", 4);

    stream_init(&s, dat, 0);
    for ( i = 16;
          (i < (mfm_bytes_per_track-15000)*8) &&
              (valid_blocks != ((1u<<6)-1));
          i++, stream_nextbit(&s) )
    {
        struct stream ados_stream;
        uint16_t raw_dat[6*513];

        if ( s.word != 0x4489 )
            continue;

        ados_stream = s;
        stream_nextbits(&ados_stream, 16);
        if ( ados_stream.word != 0x552a )
            continue;
        stream_nextbits(&ados_stream, 16);
        if ( ados_stream.word != 0xaaaa )
            continue;

        for ( j = 0; j < sizeof(raw_dat)/2; j++ )
        {
            uint16_t e, o;
            stream_nextbits(&ados_stream, 16);
            e = ados_stream.word;
            stream_nextbits(&ados_stream, 16);
            o = ados_stream.word;
            raw_dat[j] = htons(((e & 0x5555u) << 1) | (o & 0x5555u));
        }

        for ( j = 0; j < 6; j++ )
        {
            uint16_t *sec = &raw_dat[j*513];
            uint16_t csum = ntohs(*sec++), c = 0;
            for ( k = 0; k < 512; k++ )
                c += ntohs(sec[k]);
            if ( c == csum )
            {
                memcpy(&blocks[j*1024], sec, 1024);
                valid_blocks |= 1u << j;
            }
        }
    }

#if 0
    if ( valid_blocks == 0 )
    {
        free(blocks);
        return NULL;
    }
#endif

    if ( valid_blocks == ((1u<<6)-1) )
        return blocks;

    printf("Track %u: ", tracknr);

    bad = 0;
    for ( i = 0; i < 6; i++ )
        if ( !(valid_blocks & (1u<<i)) )
            bad++;
    printf("[%u missing Lemmings sectors] ", bad);

    printf("\n");

    return blocks;
}

static void check_syncs(void *dat, unsigned int tracknr)
{
    struct stream s;
    int i, j;
    static const uint16_t sync_list[] = {
        0x8a91, 0x8a44, 0x8a45, 0x8a51, 0x8912, 0x8911,
        0x8914, 0x8915, 0x8944, 0x8945, 0x8951 };

    stream_init(&s, dat, 0);
    for ( i = 16;
          (i < (mfm_bytes_per_track-15000)*8);
          i++, stream_nextbit(&s) )
    {
        for ( j = 0; j < 1/*ARRAY_SIZE(sync_list)*/; j++ )
            if ( s.word == sync_list[j] )
                printf("%u/%u: %04x\n", tracknr, i/8, s.word);
    }
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
        if ( i == 1 )
            parse_copylock_track(track, i);
        check_syncs(track, i);
        ados_blocks = parse_lemmings_track(track, i);
        put_track(track);
        if ( !is_readonly && ados_blocks )
            write_exact(out_fd, ados_blocks,
                        ADOS_BYTES_PER_BLOCK*ADOS_BLOCKS_PER_TRACK);
        free(ados_blocks);
    }

    return 0;
}
