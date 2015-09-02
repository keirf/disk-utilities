/*
 * scp.h
 * 
 * Communicate with Supercard Pro hardware to create .scp images.
 * 
 * Written in 2014 by Keir Fraser
 */

#ifndef __SCP_SCP_H__
#define __SCP_SCP_H__

#include <stdint.h>
#include <libdisk/util.h>

struct scp_handle;

struct scp_flux {
    struct {
        uint32_t index_time, nr_bitcells;
    } info[5];
    uint16_t flux[512*1024/2];
};

#define SCPCMD_SELA        0x80 /* select drive A */
#define SCPCMD_SELB        0x81 /* select drive B */
#define SCPCMD_DSELA       0x82 /* deselect drive A */
#define SCPCMD_DSELB       0x83 /* deselect drive B */
#define SCPCMD_MTRAON      0x84 /* turn motor A on */
#define SCPCMD_MTRBON      0x85 /* turn motor B on */
#define SCPCMD_MTRAOFF     0x86 /* turn motor A off */
#define SCPCMD_MTRBOFF     0x87 /* turn motor B off */
#define SCPCMD_SEEK0       0x88 /* seek track 0 */
#define SCPCMD_STEPTO      0x89 /* step to specified track */
#define SCPCMD_STEPIN      0x8a /* step towards inner (higher) track */
#define SCPCMD_STEPOUT     0x8b /* step towards outer (lower) track */
#define SCPCMD_SELDENS     0x8c /* select density */
#define SCPCMD_SIDE        0x8d /* select side */
#define SCPCMD_STATUS      0x8e /* get drive status */
#define SCPCMD_GETPARAMS   0x90 /* get parameters */
#define SCPCMD_SETPARAMS   0x91 /* set parameters */
#define SCPCMD_RAMTEST     0x92 /* do RAM test */
#define SCPCMD_SETPIN33    0x93 /* set pin 33 of floppy connector */
#define SCPCMD_READFLUX    0xa0 /* read flux level */
#define SCPCMD_GETFLUXINFO 0xa1 /* get info for last flux read */
#define SCPCMD_WRITEFLUX   0xa2 /* write flux level */
#define SCPCMD_READMFM     0xa3 /* read MFM level */
#define SCPCMD_GETMFMINFO  0xa4 /* get info for last MFM read */
#define SCPCMD_WRITEMFM    0xa5 /* write MFM level */
#define SCPCMD_READGCR     0xa6 /* read GCR level */
#define SCPCMD_GETGCRINFO  0xa7 /* get info for last GCR read */
#define SCPCMD_WRITEGCR    0xa8 /* write GCR level */
#define SCPCMD_SENDRAM_USB 0xa9 /* send data from buffer to USB */
#define SCPCMD_LOADRAM_USB 0xaa /* get data from USB and store in buffer */
#define SCPCMD_SENDRAM_232 0xab /* send data from buffer to the serial port */
#define SCPCMD_LOADRAM_232 0xac /* get data from the serial port to buffer */
#define SCPCMD_SCPINFO     0xd0 /* get SCP info */

const char *scp_errstr(unsigned int err);
const char *scp_cmdstr(unsigned int cmd);

struct scp_handle *scp_open(const char *sername);
void scp_close(struct scp_handle *scp);

void scp_send(
    struct scp_handle *scp,
    uint8_t cmd,
    void *dat,
    uint8_t len);

void scp_printinfo(struct scp_handle *scp);
void scp_ramtest(struct scp_handle *scp);
void scp_selectdrive(struct scp_handle *scp, unsigned int drv);
void scp_deselectdrive(struct scp_handle *scp, unsigned int drv);
void scp_seek_track(struct scp_handle *scp, unsigned int track);
void scp_read_flux(struct scp_handle *scp, unsigned int nr_revs,
                   struct scp_flux *flux);

struct disk_header {
    uint8_t sig[3];
    uint8_t version;
    uint8_t disk_type;
    uint8_t nr_revolutions;
    uint8_t start_track;
    uint8_t end_track;
    uint8_t flags;
    uint8_t cell_width;
    uint16_t reserved;
    uint32_t checksum;
};

#define DISKTYPE_amiga   4

#define _FLAG_index_cued 0
#define _FLAG_96tpi      1
#define _FLAG_360rpm     2
#define _FLAG_writable   3

#endif /* __SCP_SCP_H__ */
