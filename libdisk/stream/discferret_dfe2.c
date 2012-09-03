/*
 * stream/discferret_dfe2.c
 * 
 * Parse DiscFerret DFE2 format, as read directly from the device.
 * 
 * Written in 2012 by balrog, based on code by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static uint32_t read_u16_be(unsigned char *dat);
static uint32_t read_u32_be(unsigned char *dat);

struct dfe2_stream {
    struct stream s;
    int fd;

    /* Current track number. */
    unsigned int track;

    /* Raw track data. */
    unsigned char *dat;      /* track data */
    unsigned int datsz;      /* track size */
    unsigned int filesz;     /* file size */

    unsigned int dat_idx;    /* current index into dat[] */
    unsigned int stream_idx; /* current index into non-OOB data in dat[] */
    unsigned int index_pos;  /* stream_idx position of next index pulse */

    unsigned int acq_freq;
};

#define DRIVE_SPEED_UNCERTAINTY 0.05

#define FIFTY_MHZ 50000000
#define ONE_HUNDRED_MHZ 100000000
#define TWENTY_FIVE_MHZ 25000000

#define ACQ_FREQ (dfss->acq_freq)

#define SCK_PS_PER_TICK (1000000000/(ACQ_FREQ/1000))

static struct stream *dfe2_open(const char *name)
{
    struct stat sbuf;
    struct dfe2_stream *dfss;
    char magicbuf[4];
    char magic[4] = {'D','F','E','2'};
    char oldmagic[4] = {'D', 'F', 'E', 'R'};
    int fd, filesz;

    if (stat(name, &sbuf) < 0)
        return NULL;

    if ((fd = open(name, O_RDONLY)) == -1)
        err(1, "%s", name);

    read_exact(fd, magicbuf, 4);

    if (((filesz = lseek(fd, 0, SEEK_END)) < 0) ||
        (lseek(fd, 0, SEEK_SET) < 0))
        err(1, "%s", name);

    if (memcmp(magicbuf, oldmagic, 4) == 0)
        errx(1, "Old-style DFI not supported!");
    if (memcmp(magicbuf, magic, 4) != 0)
        errx(1, "%s is not a DFI file!", name);

    dfss = memalloc(sizeof(*dfss));
    dfss->fd = fd;
    dfss->filesz = filesz;

    return &dfss->s;
}

static void dfe2_close(struct stream *s)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);
    close(dfss->fd);
    memfree(dfss->dat);
    memfree(dfss);
}

/* Ugly heuristic to guess acq frequency */
static unsigned int dfe2_find_acq_freq(struct stream *s)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);
    unsigned char *dat = dfss->dat;

    unsigned int i = 0;
    uint32_t carry = 0;
    uint32_t abspos = 0;
    uint32_t index_pos = 0;

    bool_t done = 0;

    while (!done && (i < dfss->datsz)) {

        if ((dat[i] & 0x7f) == 0x7f) { /* carry */
            carry += 127;
            abspos += 127;
        } else if ((dat[i] & 0x80) != 0) {
            carry += (dat[i] & 0x7f);
            abspos += (dat[i] & 0x7f);
            index_pos = abspos;
            if(index_pos != 0)
                done = 1;
        } else {
            abspos = abspos + (dat[i] & 0x7f);
            carry = 0;
        }
        i++;
    }
    if (index_pos == 0)
        index_pos = abspos;
    
    if (abs((index_pos * 5) - TWENTY_FIVE_MHZ) < (TWENTY_FIVE_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return TWENTY_FIVE_MHZ;
    else if (abs((index_pos * 6) - TWENTY_FIVE_MHZ) < (TWENTY_FIVE_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return TWENTY_FIVE_MHZ;
    else if (abs((index_pos * 5) - FIFTY_MHZ) < (FIFTY_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return FIFTY_MHZ;
    else if (abs((index_pos * 6) - FIFTY_MHZ) < (FIFTY_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return FIFTY_MHZ;
    else if (abs((index_pos * 5) - ONE_HUNDRED_MHZ) < (ONE_HUNDRED_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return ONE_HUNDRED_MHZ;
    else if (abs((index_pos * 6) - ONE_HUNDRED_MHZ) < (ONE_HUNDRED_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return ONE_HUNDRED_MHZ;
    else {
        printf("Cannot determine acq frequency! Maybe you used a nonstandard drive! Using default of 50MHz.\n");
        return FIFTY_MHZ;
    }
}

static int dfe2_select_track(struct stream *s, unsigned int tracknr)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);

    unsigned char header[10]; /* track header */
    unsigned int curtrack;

    uint16_t cyl = 0;
    uint16_t head = 0;
    uint16_t sector = 0;
    uint32_t data_length = 0;
    off_t cur_offst;
    
    if (dfss->dat && (dfss->track == tracknr))
        return 0;

    memfree(dfss->dat);
    dfss->dat = NULL;
    
    lseek(dfss->fd, 4, SEEK_SET);
    for (curtrack = 0; curtrack <= tracknr; curtrack++) {
        if (lseek(dfss->fd, data_length, SEEK_CUR) >= dfss->filesz)
            return -1;
        read_exact(dfss->fd, header, 10);
        cur_offst += data_length + 10;
        cyl = read_u16_be(&header[0]);
        head = read_u16_be(&header[2]);
        sector = read_u16_be(&header[4]);
        data_length = read_u32_be(&header[6]);
    }
    if (tracknr != (cyl*2)+head)
        printf("DFI track number doesn't match!\n");
    if (sector != 1)
        errx(1, "Hard sectored disks are not supported!\n");

    dfss->datsz = data_length;
    dfss->dat = memalloc(data_length);
    read_exact(dfss->fd, dfss->dat, data_length);

    dfss->track = tracknr;
    dfss->acq_freq = dfe2_find_acq_freq(&dfss->s);

    return 0;
}

static void dfe2_reset(struct stream *s)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);

    dfss->dat_idx = dfss->stream_idx = 0;
    dfss->index_pos = ~0u;
    lseek(dfss->fd, 0, SEEK_SET);
}

static uint32_t read_u16_be(unsigned char *dat)
{
    return ((uint32_t)dat[0] << 8) | (uint32_t)dat[1];
}

static uint32_t read_u32_be(unsigned char *dat)
{
    return (read_u16_be(&dat[0]) << 16) | read_u16_be(&dat[2]);
}


static int dfe2_next_flux(struct stream *s)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);

    unsigned int i = dfss->dat_idx; 
    unsigned char *dat = dfss->dat;

    uint32_t carry = 0;
    uint32_t abspos = dfss->stream_idx;
    
    uint32_t val = 0, flux;
    bool_t done = 0;

    if ((dfss->stream_idx >= dfss->index_pos) || (i == 0)) {
        dfss->index_pos = ~0u;
        index_reset(s);
    }

    while (!done && (i < dfss->datsz)) {
        
        if ( dat[i] == 0xFF ) {
            errx(1, "DFI stream contained a 0xFF at track %d, position %d, THIS SHOULD NEVER HAPPEN! Bailing out!\n", dfss->track, i);
        }
        
        if ((dat[i] & 0x7f) == 0x7f) { /* carry */
            carry += 127;
            abspos += 127;
        } else if (dat[i] & 0x80) {
            carry += (dat[i] & 0x7f);
            abspos += (dat[i] & 0x7f);
            dfss->index_pos = abspos;
        } else {
            val = ((dat[i] & 0x7f) + carry);
            abspos = abspos + (dat[i] & 0x7f);
            carry = 0;
            done = 1;
        }
        i++;
    }
    if ((i == dfss->datsz) && ((abspos - dfss->index_pos) > 5))
        dfss->index_pos = abspos;
    
    dfss->stream_idx = abspos;

    dfss->dat_idx = i;

    if (!done)
        return -1;

    flux = (val * (uint32_t)SCK_PS_PER_TICK) / 1000u;
    return (int)flux;
}

struct stream_type discferret_dfe2 = {
    .open = dfe2_open,
    .close = dfe2_close,
    .select_track = dfe2_select_track,
    .reset = dfe2_reset,
    .next_bit = flux_next_bit,
    .next_flux = dfe2_next_flux,
    .suffix = { "dfi", NULL }

};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
