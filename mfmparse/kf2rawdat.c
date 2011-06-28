/******************************************************************************
 * kf2rawdat.c
 * 
 * Convert KryoFlux raw stream files to my own raw data format.
 * 
 * DTC invocation example:
 *  dtc -r6 -f<raw_base_name> -i0 -e79 -i5
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

#define BYTES_PER_TRACK (128*1024)

static char *out, *p;
static unsigned int ticks, base, bitoff;
static uint8_t nxtbyte, idx_assert;

#define MCK_FREQ (((18432000u * 73) / 14) / 2)
#define SCK_FREQ (MCK_FREQ / 2)
#define ICK_FREQ (MCK_FREQ / 16)

/* PAL Amiga CIA frequency 0.709379 MHz */
#define CIA_FREQ 709379u

#define SCK_PS_PER_TICK (1000000000u/(SCK_FREQ/1000))
#define CIA_NS_PER_TICK (1000000000u/CIA_FREQ)

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

static uint32_t read_u16(unsigned char *dat)
{
    return ((uint32_t)dat[1] << 8) | (uint32_t)dat[0];
}

static uint32_t read_u32(unsigned char *dat)
{
    return (read_u16(&dat[2]) << 16) | read_u16(&dat[0]);
}

static void shift(unsigned int val)
{
    nxtbyte <<= 1;
    nxtbyte |= val;
    bitoff++;
    if ( (bitoff == 8) && ((p-out) < BYTES_PER_TRACK) )
    {
        uint32_t cia_ticks = ticks / CIA_NS_PER_TICK;
        ticks %= CIA_NS_PER_TICK;
        *p++ = cia_ticks > 0x7f ? 0x7f: cia_ticks;
        *p++ = nxtbyte;
        if ( idx_assert )
        {
            idx_assert = 0;
            p[-2] |= 0x80;
        }
        bitoff = 0;
    }
}

static void next_val(uint32_t val)
{
    int i;

    val = (val * SCK_PS_PER_TICK) / 1000;

    for ( i = 0; val >= (base + (base>>1)); i++ )
    {
        ticks += base;
        val -= base;
        shift(0);
    }

    ticks += val;
    shift(1);

    if ( (i >= 1) && (i <= 3) )
    {
        int32_t diff = val - base;
        base += diff/10;
    }
}

static void process_track(
    unsigned char *dat, unsigned int sz, unsigned int tracknr)
{
    unsigned int i = 0, stream_idx = 0;
    uint32_t acc = 0, val = 0, count = 0, idx_pos = ~0u;

    p = out;
    bitoff = ticks = 0;
    base = 2000; /* 2us */
    idx_assert = nxtbyte = 0;

    while ( (i < sz) && ((p-out) < BYTES_PER_TRACK) )
    {
        if ( stream_idx >= idx_pos )
        {
            idx_pos = ~0u;
            idx_assert = 1;
        }

        switch ( dat[i] )
        {
        case 0x00 ... 0x07: two_byte_sample:
            count++;
            val = acc + ((uint32_t)dat[i] << 8) + dat[i+1];
            acc = 0;
            next_val(val);
            i += 2; stream_idx += 2;
            break;
        case 0x8: /* nop1 */
            i += 1; stream_idx += 1;
            break;
        case 0x9: /* nop2 */
            i += 2; stream_idx += 2;
            break;
        case 0xa: /* nop3 */
            i += 3; stream_idx += 3;
            break;
        case 0xb: /* overflow16 */
            acc += 0x10000;
            i += 1; stream_idx += 1;
            break;
        case 0xc: /* value16 */
            i += 1; stream_idx += 1;
            goto two_byte_sample;
        case 0xd: /* oob */ {
            uint32_t pos;
            uint16_t sz = read_u16(&dat[i+2]);
            i += 4;
            pos = read_u32(&dat[i+0]);
            switch ( dat[i-3] )
            {
            case 0x1: /* stream read */
            case 0x3: /* stream end */
                if ( pos != stream_idx )
                    errx(1, "Out-of-sync during track read");
                break;
            case 0x2: /* index */
                /* sys_time ticks at ick_freq */
#if 0
                printf("%u: Index %u, timer %u, sys_time %u\n",
                       count, pos, read_u32(&dat[i+4]),
                       read_u32(&dat[i+8]));
#endif
                idx_pos = pos;
                break;
            }
            i += sz;
            break;
        }
        default: /* 1-byte sample */
            count++;
            val = acc + dat[i];
            acc = 0;
            next_val(val);
            i += 1; stream_idx += 1;
            break;
        }
    }

    if ( (p-out) != BYTES_PER_TRACK )
        errx(1, "Not enough bytes from track %u", tracknr);
}

int main(int argc, char **argv)
{
    int fd, ofd, i;
    off_t sz;
    unsigned char *dat;
    unsigned int baselen;

    if ( argc != 3 )
        errx(1, "Usage: kf2rawdat <raw_base_name> <dest_file>");

    baselen = strlen(argv[1]);

    ofd = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if ( ofd == -1 )
        err(1, "%s", argv[2]);

    if ( (out = malloc(BYTES_PER_TRACK)) == NULL )
        err(1, NULL);

    for ( i = 0; i < 160; i++ )
    {
        char srcname[baselen + 9];
        sprintf(srcname, "%s%02u.%u.raw", argv[1], i>>1, i&1);

        fd = open(srcname, O_RDONLY);
        if ( fd == -1 )
            err(1, "%s", srcname);

        sz = lseek(fd, 0, SEEK_END);
        if ( sz < 0 )
            err(1, "%s", srcname);

        if ( (dat = malloc(sz)) == NULL )
            err(1, NULL);

        lseek(fd, 0, SEEK_SET);
        read_exact(fd, dat, sz);

        process_track(dat, sz, i);

        write_exact(ofd, out, BYTES_PER_TRACK);

        free(dat);
        close(fd);
    }

    close(ofd);
    free(out);

    return 0;
}
