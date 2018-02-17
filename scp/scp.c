/*
 * scp.c
 * 
 * Communicate with Supercard Pro hardware to create .scp images.
 * 
 * Written in 2014 by Keir Fraser
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

#include "scp.h"

#include "../libdisk/util.c"

const struct scp_params default_scp_params = {
    .select_delay_ms = 1,
    .step_delay_ms = 5,
    .motoron_delay_ms = 750,
    .seek0_delay_ms = 15,
    .deselect_delay_ms = 20000,
    .seek_settle_delay_ms = 20
};

struct scp_handle {
    int fd;
    char *sername;
    struct termios oldtio, newtio;
    struct scp_params scp_params;
};

static const char *scp_err[] = {
    "Unused",
    "BadCommand",
    "CommandErr",
    "Checksum",
    "Timeout",
    "NoTrk0",
    "NoDriveSel",
    "NoMotorSel",
    "NotReady",
    "NoIndex",
    "ZeroRevs",
    "ReadTooLong",
    "BadLength",
    "BadData",
    "BoundaryOdd",
    "WPEnabled",
    "BadRAM",
    "NoDisk"
};

static const char *scp_cmd[] = {
    "Select A",       /* 0x80 */
    "Select B",
    "Deselect A",
    "Deselect B",
    "Motor A On",
    "Motor B On",
    "Motor A Off",
    "Motor B Off",
    "Seek Cyl 0",
    "Step To Cyl",
    "Step In",
    "Step Out",
    "Select Density",
    "Select Side",
    "Get Drive Status",
    "Get Parameters", /* 0x90 */
    "Set Parameters",
    "RAM Test",
    "Set Pin 33",
    "Read Flux",      /* 0xa0 */
    "Get Flux Info",
    "Write Flux",
    "Read MFM",
    "Get MFM Info",
    "Write MFM",
    "Read GCR",
    "Get GCR Info",
    "Write GCR",
    "Read SRAM via USB",
    "Write SRAM via USB",
    "Read SRAM via 232",
    "Write SRAM via 232",
    "Get SCP Info"    /* 0xd0 */
};

const char *scp_errstr(unsigned int err)
{
    return (err < ARRAY_SIZE(scp_err)) ? scp_err[err] : "???";
}

const char *scp_cmdstr(unsigned int cmd)
{
    if (cmd >= 0x80 && cmd <= 0x8e)
        cmd -= 0x80;
    else if (cmd >= 0x90 && cmd <= 0x93)
        cmd -= 0x90 - 0xf;
    else if (cmd >= 0xa0 && cmd <= 0xac)
        cmd -= 0xa0 - 0x4 - 0xf;
    else if (cmd >= 0xd0 && cmd <= 0xd0)
        cmd -= 0xd0 - 0xd - 0x4 - 0xf;
    else
        return "???";
    return scp_cmd[cmd];
}

struct scp_handle *scp_open(const char *sername)
{
    struct scp_handle *scp = memalloc(sizeof(*scp) + strlen(sername) + 1);

    scp->sername = (char *)(scp + 1);
    strcpy(scp->sername, sername);

    /* Not a controlling tty: CTRL-C shouldn't kill us. */
    scp->fd = open(sername, O_RDWR | O_NOCTTY);
    if (scp->fd < 0)
        err(1, "Error opening %s", sername);

    if (tcgetattr(scp->fd, &scp->oldtio))
        err(1, "%s", sername);

    memcpy(&scp->newtio, &scp->oldtio, sizeof(scp->oldtio));
    scp->newtio.c_cflag = B38400 | CRTSCTS | CS8 | CLOCAL | CREAD;
    scp->newtio.c_iflag = IGNBRK | IGNPAR;
    scp->newtio.c_oflag = 0; 
    scp->newtio.c_lflag = 0;
    scp->newtio.c_cc[VMIN] = 1;
    scp->newtio.c_cc[VTIME] = 0;

    if (tcflush(scp->fd, TCIOFLUSH) ||
        tcsetattr(scp->fd, TCSANOW, &scp->newtio))
        err(1, "%s", sername);

    scp_set_params(scp, &default_scp_params);

    return scp;
}

void scp_close(struct scp_handle *scp)
{
    scp_set_params(scp, &default_scp_params);

    if (tcsetattr(scp->fd, TCSANOW, &scp->oldtio) || close(scp->fd))
        err(1, "%s", scp->sername);
    memfree(scp);
}

void scp_send(
    struct scp_handle *scp,
    uint8_t cmd,
    void *dat,
    uint8_t len)
{
    uint8_t *buf = memalloc(len + 3);
    unsigned int i;

    buf[0] = cmd;
    buf[1] = len;
    memcpy(buf + 2, dat, len);
    buf[len + 2] = 0x4a;
    for (i = 0; i < len + 2; i++)
        buf[len + 2] += buf[i];

    write_exact(scp->fd, buf, len + 3);

    if (cmd == SCPCMD_SENDRAM_USB) {
        read_exact(scp->fd, dat, 512*1024);
    } else if (cmd == SCPCMD_LOADRAM_USB) {
        uint32_t *ramcmd = dat;
        uint32_t len = be32toh(ramcmd[1]);
        write_exact(scp->fd, &ramcmd[2], len);
    }

    read_exact(scp->fd, buf, 2);
    if (buf[0] != cmd)
        errx(1, "Mismatch command echo: sent %02x (%s), received %02x (%s)",
             cmd, scp_cmdstr(cmd), buf[0], scp_cmdstr(buf[0]));
    if (buf[1] != 0x4f)
        errx(1, "Command %02x (%s) failed: %02x (%s)",
             cmd, scp_cmdstr(cmd), buf[1], scp_errstr(buf[1]));

    memfree(buf);
}

void scp_printinfo(struct scp_handle *scp)
{
    uint8_t info[2];
    scp_send(scp, SCPCMD_SCPINFO, NULL, 0);
    read_exact(scp->fd, info, 2);
    printf("Supercard Pro: Hardware v%u.%u; Firmware v%u.%u\n",
           info[0] >> 4, info[0] & 15, info[1] >> 4, info[1] & 15);
}

void scp_getinfo(struct scp_handle *scp, void* info)
{
    uint8_t info_tmp[2];
    scp_send(scp, SCPCMD_SCPINFO, NULL, 0);
    read_exact(scp->fd, info, 2);
    memcpy(info, &info_tmp, 2);
}

void scp_ramtest(struct scp_handle *scp)
{
    scp_send(scp, SCPCMD_RAMTEST, NULL, 0);
}

void scp_selectdrive(struct scp_handle *scp, unsigned int drv)
{
    scp_send(scp, SCPCMD_SELA + drv, NULL, 0);
    scp_send(scp, SCPCMD_MTRAON + drv, NULL, 0);
}

void scp_deselectdrive(struct scp_handle *scp, unsigned int drv)
{
    scp_send(scp, SCPCMD_MTRAOFF + drv, NULL, 0);
    scp_send(scp, SCPCMD_DSELA + drv, NULL, 0);
}

void scp_seek_track(struct scp_handle *scp, unsigned int track,
                    int double_step)
{
    struct scp_params *p = &scp->scp_params;
    uint8_t cyl = track >> 1, side = track & 1;
    if (double_step)
        cyl *= 2;
    if (cyl == 0)
        scp_send(scp, SCPCMD_SEEK0, NULL, 0);
    else
        scp_send(scp, SCPCMD_STEPTO, &cyl, 1);
    scp_send(scp, SCPCMD_SIDE, &side, 1);
    
    if (p->seek_settle_delay_ms > p->step_delay_ms) {
        uint32_t extra_ms = p->seek_settle_delay_ms - p->step_delay_ms;
        usleep(extra_ms * 1000);
    }
}

void scp_read_flux(struct scp_handle *scp, unsigned int nr_revs,
                   struct scp_flux *flux)
{
    uint8_t info[2] = { nr_revs, 1 /* wait for index */};
    unsigned int i;

    scp_send(scp, SCPCMD_READFLUX, &info, 2);

    scp_send(scp, SCPCMD_GETFLUXINFO, NULL, 0);
    read_exact(scp->fd, flux->info, sizeof(flux->info));

    for (i = 0; i < ARRAY_SIZE(flux->info); i++) {
        flux->info[i].index_time = be32toh(flux->info[i].index_time);
        flux->info[i].nr_bitcells = be32toh(flux->info[i].nr_bitcells);
    }

    *(uint32_t *)&flux->flux[0] = htobe32(0);
    *(uint32_t *)&flux->flux[2] = htobe32(512*1024);
    scp_send(scp, SCPCMD_SENDRAM_USB, flux->flux, 8);
}

void scp_write_flux(struct scp_handle *scp, void *dat, unsigned int nr_dat)
{
    uint32_t ramcmd[256*1024/4];

    ramcmd[0] = htobe32(0);
    ramcmd[1] = htobe32(nr_dat * 2);
    memcpy(&ramcmd[2], dat, nr_dat * 2);
    scp_send(scp, SCPCMD_LOADRAM_USB, ramcmd, 8);

    ramcmd[0] = htobe32(nr_dat);
    *(uint8_t *)(&ramcmd[1]) = 5; /* wait for index, wipe track */
    scp_send(scp, SCPCMD_WRITEFLUX, ramcmd, 5);
}

void scp_set_params(struct scp_handle *scp, const struct scp_params *params)
{
    uint16_t raw_params[5];

    scp->scp_params = *params;

    raw_params[0] = htobe16(params->select_delay_ms*1000);
    raw_params[1] = htobe16(params->step_delay_ms*1000);
    raw_params[2] = htobe16(params->motoron_delay_ms);
    raw_params[3] = htobe16(params->seek0_delay_ms);
    raw_params[4] = htobe16(params->deselect_delay_ms);

    scp_send(scp, SCPCMD_SETPARAMS, raw_params, sizeof(raw_params));
}
