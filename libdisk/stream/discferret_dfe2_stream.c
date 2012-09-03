/*
 * stream/discferret_dfe2_stream.c
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

    int flux;                /* Nanoseconds to next flux reversal */
    int clock, clock_centre; /* Clock base value in nanoseconds */
    unsigned int clocked_zeros;
    unsigned int acq_freq;
};

#define DRIVE_SPEED_UNCERTAINTY 0.05

#define FIFTY_MHZ 50000000
#define ONE_HUNDRED_MHZ 100000000
#define TWENTY_FIVE_MHZ 25000000

#define ACQ_FREQ (dfss->acq_freq)

#define SCK_PS_PER_TICK (1000000000/(ACQ_FREQ/1000))

#define CLOCK_CENTRE  2000   /* 2000ns = 2us */
#define CLOCK_MAX_ADJ 10     /* +/- 10% adjustment */
#define CLOCK_MIN(_c) (((_c) * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX(_c) (((_c) * (100 + CLOCK_MAX_ADJ)) / 100)

static struct stream *dfe2_open(const char *name)
{
    struct stat sbuf;
    struct dfe2_stream *dfss;
    char magicbuf[4];
    char magic[4] = {'D','F','E','2'};
    char oldmagic[4] = {'D', 'F', 'E', 'R'};
    int fd;
    int filesz;

    if(stat(name, &sbuf) < 0) {
        return NULL;
    }
    
    if ((fd = open(name, O_RDONLY)) == -1)
        err(1, "%s", name);
        
    read_exact(fd, magicbuf, 4);
    
    filesz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    if(memcmp(magicbuf, oldmagic, 4) == 0) {
        errx(1, "Old-style DFI not supported!");
    }
    if(memcmp(magicbuf, magic, 4) != 0)
        errx(1, "%s is not a DFI file!", name);
    
    dfss = memalloc(sizeof(*dfss));
    dfss->fd = fd;
    dfss->filesz = filesz;
    
    dfss->clock = dfss->clock_centre = CLOCK_CENTRE;
    
    return &dfss->s;
    
}

static void dfe2_close(struct stream *s)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);
    close(dfss->fd);
    memfree(dfss->dat);
    memfree(dfss);
}

static void dfe2_set_density(struct stream *s, unsigned int ns_per_cell)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);
    dfss->clock = dfss->clock_centre = ns_per_cell;
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

        if((dat[i] & 0x7f) == 0x7f) { /* carry */
            carry += 127;
            abspos += 127;
        }
        else if((dat[i] & 0x80) != 0) {
            carry += (dat[i] & 0x7f);
            abspos += (dat[i] & 0x7f);
            index_pos = abspos;
            if(index_pos != 0)
                done = 1;
        }
        else {
            abspos = abspos + (dat[i] & 0x7f);
            carry = 0;
        }
        i++;
    }
    if(index_pos == 0)
        index_pos = abspos;
    
    if(abs((index_pos * 5) - TWENTY_FIVE_MHZ) < (TWENTY_FIVE_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return TWENTY_FIVE_MHZ;
    else if(abs((index_pos * 6) - TWENTY_FIVE_MHZ) < (TWENTY_FIVE_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return TWENTY_FIVE_MHZ;
    else if(abs((index_pos * 5) - FIFTY_MHZ) < (FIFTY_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return FIFTY_MHZ;
    else if(abs((index_pos * 6) - FIFTY_MHZ) < (FIFTY_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return FIFTY_MHZ;
    else if(abs((index_pos * 5) - ONE_HUNDRED_MHZ) < (ONE_HUNDRED_MHZ * DRIVE_SPEED_UNCERTAINTY))
        return ONE_HUNDRED_MHZ;
    else if(abs((index_pos * 6) - ONE_HUNDRED_MHZ) < (ONE_HUNDRED_MHZ * DRIVE_SPEED_UNCERTAINTY))
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
    for(curtrack = 0; curtrack <= tracknr; curtrack++) {
        if(lseek(dfss->fd, data_length, SEEK_CUR) >= dfss->filesz)
            return -1;
        read_exact(dfss->fd, header, 10);
        cur_offst += data_length + 10;
        cyl = read_u16_be(&header[0]);
        head = read_u16_be(&header[2]);
        sector = read_u16_be(&header[4]);
        data_length = read_u32_be(&header[6]);
    }
    if(tracknr != (cyl*2)+head)
        printf("DFI track number doesn't match!\n");
    if(sector != 1)
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

    dfss->dat_idx = dfss->stream_idx = dfss->flux = dfss->clocked_zeros = 0;
    dfss->index_pos = ~0u;
    dfss->clock = dfss->clock_centre;
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


static bool_t dfe2_next_flux(struct stream *s, uint32_t *p_flux)
{
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);

    unsigned int i = dfss->dat_idx; 
    unsigned char *dat = dfss->dat;

    uint32_t carry = 0;
    uint32_t abspos = dfss->stream_idx;
    
    uint32_t val = 0;
    bool_t done = 0;

    if (dfss->stream_idx >= dfss->index_pos || i == 0) {
        dfss->index_pos = ~0u;
        index_reset(s);
    }

    while (!done && (i < dfss->datsz)) {
        
        if (dat[i] == 0xFF) {
            errx(1, "DFI stream contained a 0xFF at track %d, position %d, THIS SHOULD NEVER HAPPEN! Bailing out!\n", dfss->track, i);
        }
        
        if((dat[i] & 0x7f) == 0x7f) { /* carry */
            carry += 127;
            abspos += 127;
        }
        else if(dat[i] & 0x80) {
            carry += (dat[i] & 0x7f);
            abspos += (dat[i] & 0x7f);
            dfss->index_pos = abspos;
        }
        else {
            val = ((dat[i] & 0x7f) + carry);
            abspos = abspos + (dat[i] & 0x7f);
            carry = 0;
            done = 1;
        }
        i++;
    }
    if(i == dfss->datsz && (abspos - dfss->index_pos > 5 )) {
        dfss->index_pos = abspos;
    }
    
    dfss->stream_idx = abspos;

    dfss->dat_idx = i;

    *p_flux = val;
    return done;
}

static int dfe2_next_bit(struct stream *s)
{    
    struct dfe2_stream *dfss = container_of(s, struct dfe2_stream, s);
    int new_flux;

    while (dfss->flux < (dfss->clock/2)) {
        uint32_t flux;
        if (!dfe2_next_flux(s, &flux))
            return -1;
        dfss->flux += (flux * (uint32_t)SCK_PS_PER_TICK) / 1000u;
        dfss->clocked_zeros = 0;
    }

    s->latency += dfss->clock;
    dfss->flux -= dfss->clock;

    if (dfss->flux >= (dfss->clock/2)) {
        dfss->clocked_zeros++;
        return 0;
    }

    if (s->pll_mode != PLL_fixed_clock) {
        /* PLL: Adjust clock frequency according to phase mismatch. */
        if ((dfss->clocked_zeros >= 1) && (dfss->clocked_zeros <= 3)) {
            /* In sync: adjust base clock by 10% of phase mismatch. */
            int diff = dfss->flux / (int)(dfss->clocked_zeros + 1);
            dfss->clock += diff / 10;
        } else {
            /* Out of sync: adjust base clock towards centre. */
            dfss->clock += (dfss->clock_centre - dfss->clock) / 10;
        }

        /* Clamp the clock's adjustment range. */
        dfss->clock = max(CLOCK_MIN(dfss->clock_centre),
                          min(CLOCK_MAX(dfss->clock_centre), dfss->clock));
    } else {
        dfss->clock = dfss->clock_centre;
    }

    /* Authentic PLL: Do not snap the timing window to each flux transition. */
    new_flux = (s->pll_mode == PLL_authentic) ? dfss->flux / 2 : 0;
    s->latency += dfss->flux - new_flux;
    dfss->flux = new_flux;

    return 1;
}

struct stream_type discferret_dfe2_stream = {
    .open = dfe2_open,
    .close = dfe2_close,
    .set_density = dfe2_set_density,
    .select_track = dfe2_select_track,
    .reset = dfe2_reset,
    .next_bit = dfe2_next_bit,
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
