#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <devices/trackdisk.h>
#include <hardware/custom.h>
#include <hardware/cia.h>

static unsigned short unit;
static char drivename[5] = "DFx:";

/* Custom chip bases. */
static struct Custom *custom = (struct Custom *)0xdff000;
static struct CIA *ciaa = (struct CIA *)0xbfe001;
static struct CIA *ciab = (struct CIA *)0xbfd000;

/* Trackdisk.device handles. */
static struct IOExtTD *req_drive;
static struct MsgPort *port_drive;

/* Remove this to use DOS Inhibit() to mark drive busy. */
#define Inhibit(x) 1 /* Inhibit(x, DOSTRUE) */
#define UnInhibit(x) do {} while (0) /* Inhibit(x, DOSFALSE) */

static void release_drive(void)
{
    /* Motor off. */
    req_drive->iotd_Req.io_Command = TD_MOTOR;
    req_drive->iotd_Req.io_Length = 0;
    (void)DoIO((struct IORequest *)req_drive);

    /* Clean up. */
    UnInhibit(drivename);
    CloseDevice((struct IORequest *)req_drive);
    DeleteExtIO((struct IORequest *)req_drive);
    DeletePort(port_drive);
}

static void acquire_drive(void)
{
    if ((port_drive = CreatePort(NULL, NULL)) == NULL)
        goto fail1;

    if ((req_drive = (struct IOExtTD *)
         CreateExtIO(port_drive, sizeof(*req_drive))) == NULL)
        goto fail2;

    if (OpenDevice(TD_NAME, unit, (struct IORequest *)req_drive, 0) != 0)
        goto fail3;

    if (Inhibit(drivename) == 0)
        goto fail4;

    req_drive->iotd_Req.io_Command = TD_CHANGESTATE;
    req_drive->iotd_Req.io_Flags = IOF_QUICK;
    if ((DoIO((struct IORequest *)req_drive) != 0) ||
        (req_drive->iotd_Req.io_Actual != 0)) {
        fprintf(stderr, "Insert disk in %s and retry.\n", drivename);
        goto fail5;
    }

    /* Track 0, motor on. We'll return here when done. */
    req_drive->iotd_Req.io_Command = TD_SEEK;
    req_drive->iotd_Req.io_Offset = 0;
    (void)DoIO((struct IORequest *)req_drive);
    req_drive->iotd_Req.io_Command = TD_MOTOR;
    req_drive->iotd_Req.io_Length = 1;
    (void)DoIO((struct IORequest *)req_drive);
    Delay(500/20); /* 500ms for motor to spin up */

    /* success: */
    return;

fail5:
    UnInhibit(drivename);
fail4:
    CloseDevice((struct IORequest *)req_drive);
fail3:
    DeleteExtIO((struct IORequest *)req_drive);
fail2:
    DeletePort(port_drive);
fail1:
    fprintf(stderr, "Failed to grab %s\n", drivename);
    exit(1);
}

/* CIA_DIVISOR = ceil(NTSC_Rate/MHz * 256); NTSC_Rate = 0.715909MHz */
#define CIA_DIVISOR 184

static void cia_delay_ms(unsigned int ms)
{
    unsigned int ticks = (ms * 1000 * CIA_DIVISOR) >> 8;
    UBYTE dummy;

    ciab->ciacra &= 0xc0;
    ciab->ciacra |= CIACRAF_RUNMODE;
    dummy = ciab->ciaicr; /* clear CIAICR data */
    ciab->ciatalo = (UBYTE)ticks;
    ciab->ciatahi = (UBYTE)(ticks >> 8);

    while (!(ciab->ciaicr & CIAICRF_TA))
        continue;
}

static void seek_track0(void)
{
    /* Set direction and settle. */
    ciab->ciaprb |= CIAF_DSKDIREC;
    cia_delay_ms(18);

    while (ciaa->ciapra & CIAF_DSKTRACK0) {
        /* Step the heads inwards one track. */
        ciab->ciaprb &= (UBYTE)~CIAF_DSKSTEP;
        ciab->ciaprb |= CIAF_DSKSTEP;
        cia_delay_ms(3);
    }

    cia_delay_ms(15);
}

static void wait_dskrdy(void)
{
    int i;

    /* Wait up to 500ms for dskrdy signal. */
    for (i = 0; i < 50; i++) {
        if (!(ciaa->ciapra & CIAF_DSKRDY))
            break; /* dskrdy asserted */
        cia_delay_ms(10);
    }
}

extern void __asm grab_track(
    register __a0 unsigned char *dat,
    register __d0 unsigned int count);

#define BYTES_PER_TRACK (128*1024)
#define TRACK_BUFFER_SIZE BYTES_PER_TRACK/2*3

int main(int argc, char **argv)
{
    FILE *fp;
    UWORD dmacon, intena, adkcon;
    int track;
    unsigned char *dat;

    if ((argc != 2) && (argc != 3)) {
        fprintf(stderr, "Usage: diskread <target_filename> [drive #]\n");
        exit(1);
    }

    if (argc == 3)
        unit = *argv[2] - '0';
    drivename[2] = '0' + unit;

    acquire_drive();

    if ((fp = fopen(argv[1], "wb")) == NULL) {
        fprintf(stderr, "Could not open file \"%s\"\n", argv[1]);
        release_drive();
        exit(1);
    }

    if ((dat = malloc(TRACK_BUFFER_SIZE)) == NULL) {
        fprintf(stderr, "Could not alloc %u bytes\n", TRACK_BUFFER_SIZE);
        fclose(fp);
        release_drive();
        exit(1);
    }

    printf("Reading %s track 0", drivename);

    for (track = 0; track < 160; track++) {
        if (track > 10)
            putchar('\b');
        if (track > 100)
            putchar('\b');
        printf("\b%d", track);
        fflush(stdout);

        intena = custom->intenar;
        custom->intena = 0x7fff;
        dmacon = custom->dmaconr;
        custom->dmacon = 0x7fff;
        adkcon = custom->adkconr;
        custom->adkcon = 0x7f00;
        custom->adkcon = 0x9100; /* no precomp, no word sync, MFM */
        custom->dsklen = 0;

        /* Motor on, drive 0, seek inwards, side 0.
         * NB. We must do this as a three-step process:
         *  1. Deselect all drives
         *  2. Assert motor-on line
         *  3. Select required drive
         * As drives sample the motor-on signal only on the asserting edge of 
         * their select signal. If a drive is selected in the same write cycle 
         * as the motor-on signal is asserted, some drives will sample the 
         * motor-on signal too early and turn *off* their motor! */
        ciab->ciaprb |= 0x78;
        ciab->ciaprb = (UBYTE)~CIAF_DSKMOTOR;
        ciab->ciaprb = (UBYTE)~(CIAF_DSKMOTOR
                                | (1u << (CIAB_DSKSEL0 + unit))
                                | CIAF_DSKDIREC);

        wait_dskrdy();

        if (track & 1) {
            /* Side 1. */
            ciab->ciaprb &= ~(UBYTE)CIAF_DSKSIDE;
        } else if (track == 0) {
            seek_track0();
        } else {
            /* Step the heads inwards one track. */
            ciab->ciaprb &= (UBYTE)~CIAF_DSKSTEP;
            ciab->ciaprb |= CIAF_DSKSTEP;
            cia_delay_ms(18);
        }

        /* Full-range free-running CIAB timer A. */
        ciab->ciacra &= 0xc0;
        ciab->ciatalo = 0xff;
        ciab->ciatahi = 0xff;
        ciab->ciacra |= CIACRAF_START;

        grab_track(dat, BYTES_PER_TRACK);

        /* Stop CIAB timer A. */
        ciab->ciacra &= 0xc0;

        if (track == 159)
            seek_track0();

        custom->adkcon = 0x7f00;
        custom->adkcon = 0x8000 | adkcon;
        custom->dmacon = 0x7fff;
        custom->dmacon = 0x8000 | dmacon;
        custom->intena = 0x7fff;
        custom->intena = 0x8000 | intena;

        fwrite(dat, BYTES_PER_TRACK, 1, fp);
    }

    printf("\n");

    free(dat);
    fclose(fp);
    release_drive();

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
