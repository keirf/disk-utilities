/*
 * libdisk/container/jv3.c
 * 
 * Write JV3 images (jv3 headers and dump of logical sector contents).
 * 
 * Contributed by Mike Gore 2016 
 * Based on work done 2012 by Keir Fraser
 *
 * Testing as of Feb 10 2016
 *
 *    Case 1:
 *        80 track, double sided, 18 sector, MFM
 *             "double density" TRS80 disk from 1982
 *        Kryoflux read on FD-55GRF 80 track drive
 *        # Read from Kryoflux
 *        dtc -d1 -p -fdisk/disk -i0 -i2 
 *        # Convert to CT format
 *        dtc -m1 -p -fdisk/disk -i0 -fdisk_trs80_dd.ct -i2 -l0
 *        # Convert to JV3 format
 *        disk-analyse -v  -f trs80 disk_trs80_dd.ct disk_trs80_dd.jv3
 *    Case 2:
 *        35 track, single sided, 10 sector, FM
 *             "single density" TRS80 disk from 1982
 *        Kryoflux read on FD-55GRF 80 track drive
 *        # Read from Kryoflux
 *        dtc -d1 -p -fdisk/disk -i0 -i2 
 *        # Convert to CT format
 *        dtc -m1 -p -fdisk/disk -i0 -fdisk_trs80_sd.ct -i2 -l0
 *        # Convert to JV3 format
 *        disk-analyse -v  -f trs80 disk_trs80_sd.ct disk_trs80_sd.jv3
 *
 */

#include <libdisk/util.h>
#include <private/disk.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef JV3_DEBUG
#define JV3_DEBUG 0
#endif
#define JV3_TRC(f, a...) if (JV3_DEBUG) printf(f, ##a)

/* ===================================
 * JV3 Documenation from http://www.tim-mann.org/trs80/dskspec.html
 * Tim Mann http://tim-mann.org */

/* Todo: 
 *  add more error information in the jv3 header processing */

/*
 * Layout of JV3
 * Note: we only are doing 80 track double density trsdos/ldos disks
 * so we do not need the double header option
 typedef struct {
 unsigned char track;
 unsigned char sector;
 unsigned char flags;
 } SectorHeader;

 typedef struct {
 SectorHeader sh[JV3_ENTRIES];
 unsigned char writeprot;
 } JV3;
*/

#define JV3_DENSITY     0x80  /* 1=dden, 0=sden */
#define JV3_DAM         0x60  /* data address mark code; see below */
#define JV3_SIDE        0x10  /* 0=side 0, 1=side 1 */
#define JV3_ERROR       0x08  /* 0=ok, 1=CRC error */
#define JV3_NONIBM      0x04  /* 0=normal, 1=short */
#define JV3_SIZE        0x03  /* in used sectors: 0=256,1=128,2=1024,3=512
                                 in free sectors: 0=512,1=1024,2=128,3=256 */
/* for now we just use 0xff for all entries not on the defined disk */
#define JV3_FREE        0xFF  /* in track and sector fields of free sectors */
#define JV3_FREEF       0xFC  /* in flags field, or'd with size code */

#define JV3_ENTRIES 2901
/* exact jv3 header size includes one byte flags at the end */
#define JV3_HEADER_SIZE (JV3_ENTRIES*3+1)

unsigned char jv3_buf[JV3_HEADER_SIZE+3]; /* add a few bytes of overflow */

/* see imd.c */
enum {
    MODE_fm_500kbps = 0,
    MODE_fm_300kbps = 1,
    MODE_fm_250kbps = 2,
    MODE_mfm_500kbps = 3,
    MODE_mfm_300kbps = 4,
    MODE_mfm_250kbps = 5
};

struct track_header {
    uint8_t mode, cyl, head, sec, nr_secs, sec_sz;
};

static struct container *jv3_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

static unsigned int mark_to_flags(uint8_t mark, unsigned int density)
{
    /* WD1771 and TRS80 address mark usage
     *  JV3_DAM value   Single density          Double density
     *  0x00            0xFB (Normal)           0xFB (Normal)
     *  0x20            0xFA (User-defined)     0xF8 (Deleted)
     *  0x40            0xF9 (User-defined)     Invalid; unused
     *  0x60            0xF8 (Deleted)          Invalid; unused
     * Early TRS80 Directory Marks were extra Address marks
     * They actually used the deleted address mark as one of them!
     * (that mark was intended for mapping out bad areas of the disk)
     * Here we only consider 0xfa and 0xf8
     * Note: Normal mark is 0 so flags are unchanged for that value */
    unsigned int flags = 0;
    if (!density) { /* Single density */
        if (mark == 0xfa)
            flags |= 0x20;
        else if (mark == 0xf9)
            flags |= 0x40;
        else if (mark == 0xf8)
            flags |= 0x60;
    } else { /* Double Density */
        if (mark == 0xf8)
            flags |= 0x20;
    }
    return flags;
}

/* Write out the JV3 header and raw sector dump */
static void jv3_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct track_info *ti;
    struct track_header thdr;
    uint8_t *secs, *cyls, *heads, *nos, *marks, *dat;
    unsigned int trk, sec, mark,sec_sz;
    unsigned int flags;
    unsigned int density;
    unsigned int jv3_state;
    int jv3_ind = 0;
    int step = 0;
    int unformatted = 0;

    /* truncate to 0 bytes */
    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    /* Loop twice - first pass dumps headers - second pass secors
     * Fill jv3_buf in jv3_state 0
     * Write jv3_but at start of jv3_state 1 - then write sectors */
    for (jv3_state=0; jv3_state<2; jv3_state++) {
        /* Are we done with the header? */
        if (jv3_state == 1 && jv3_ind)
        {
            /* PAD and WRITE PROTECT BYTE */
            while (jv3_ind < JV3_HEADER_SIZE)
                jv3_buf[jv3_ind++] = 0xff;
            write_exact(d->fd, jv3_buf, JV3_HEADER_SIZE);
            jv3_ind = 0; /* ignore header now it's written */
        }

        /* Look at the imd.c container for data access examples */

        /* FIXME
         * Test for double step
         *    (80 track drive reading 40 track disk) */
        unformatted = 0;
        for (trk = 0; trk < di->nr_tracks; trk++) {
            ti = &di->track[trk];
            if (ti->type == TRKTYP_unformatted)
                unformatted++;
        }

        step = 1;
        if (unformatted > trk/2)
            step = 2;

        JV3_TRC("============\nstep:%d\n===========\n", step);

        for (trk = 0; trk < di->nr_tracks; trk++) {
            ti = &di->track[trk];

            thdr.mode = 0xff;
            density = 0;
            /* FIXME
             * density flag for JV3
             * format / bit rate testing borrowed directky from imd.c */
            switch (ti->type) {
            case TRKTYP_trs80_fm_sd:
                thdr.mode = MODE_fm_250kbps;
                break;
            case TRKTYP_trs80_mfm_dd:
                density = 1;
                thdr.mode = MODE_mfm_250kbps;
                break;
            case TRKTYP_ibm_fm_sd:
                thdr.mode = MODE_fm_250kbps;
                break;
            case TRKTYP_ibm_fm_dd:
                thdr.mode = MODE_fm_500kbps;
                break;
            case TRKTYP_ibm_mfm_dd:
                density = 1;
                thdr.mode = MODE_mfm_250kbps;
                break;
            case TRKTYP_ibm_mfm_hd:
                density = 1;
                thdr.mode = MODE_mfm_500kbps;
                break;
            case TRKTYP_unformatted:
                break;
            default:
                warnx("T%u.%u: Ignoring track format '%s' while "
                      "writing JV3 file",
                      cyl(trk)/step, hd(trk), ti->typename);
                break;
            }

            if ((thdr.mode == 0xff) || !ti->nr_sectors)
                continue;
    
            if ((uint32_t)ti->nr_sectors >= 256) {
                warnx("T%u.%u: Unexpected number of IBM-MFM sectors (%u)",
                      cyl(trk)/step, hd(trk), ti->nr_sectors);
                continue;
            }
    
            /* This pulls information from the helper. In the case of jV3 we 
             * borrow use the ibm.c functions with extra trs80 address marks.
             * The source can be from server file types - only tested with
             * KroFlux CT type. */
            retrieve_ibm_mfm_track(
                d, trk, &secs, &cyls, &heads, &nos, &marks, &dat);

            /* FIXME cyl calculation */
            /*thdr.cyl = cyls[0];*/
            /*thdr.head = heads[0];*/
            thdr.cyl = cyl(trk);
            thdr.head = hd(trk);
            thdr.nr_secs = ti->nr_sectors;
            thdr.sec_sz = nos[0];
            sec_sz = 128u << thdr.sec_sz;
    
            JV3_TRC("trk:%d,cyl:%d,head:%d,secs:%d,sec_sz:%d,"
                    "mode:%d,density:%d\n",
                    trk, thdr.cyl, thdr.head, thdr.nr_secs, sec_sz,
                    thdr.mode,density);
    
            for (sec = 0; sec < ti->nr_sectors; sec++) {
                JV3_TRC(" sec:%d,mark:%02x", secs[sec],marks[sec]);
                if (nos[sec] != thdr.sec_sz) {
                    warnx("T%u.%u: Cannot write mixed-sized sectors to "
                          "JV3 file", cyl(trk), hd(trk));
                    break;
                }
                
                if (cyl(cyls[sec]) != thdr.cyl)
                    thdr.head |= 0x80;
                if (hd(heads[sec]) != (thdr.head&1))
                    thdr.head |= 0x40;
            }
            JV3_TRC("\n");
            if (sec == ti->nr_sectors) {
                for (sec = 0; sec < ti->nr_sectors; sec++) {
                    /* write JV3 headers first at state 0 */
                    if (jv3_state == 0) {
                        if(jv3_ind >= JV3_HEADER_SIZE) {
                            printf("jv3 header index exceeded:%04x\n",
                                   jv3_ind);
                            printf("\ttrack:%d,side:%d,sector:%d\n",
                                   cyls[sec],heads[sec] & 1,sec);
                            /* FIXME */
                            exit(1);
                        }

                        /* address marks */
                        mark = marks[sec];
                        /* map cylinder */
                        jv3_buf[jv3_ind++] = cyls[sec];
                        /* map sector number */
                        jv3_buf[jv3_ind++] = secs[sec];

                        /* flags */
                        flags = (density ? JV3_DENSITY : 0);
                        flags |= (( heads[sec] & 1) ? JV3_SIDE : 0);

                        flags |= mark_to_flags(mark, density);
                        jv3_buf[jv3_ind++] = flags;
                    } else { /* jv3_state == 1 */
                        write_exact(d->fd, &dat[sec*sec_sz], sec_sz);
                    }
                }
            }
            memfree(secs);
            memfree(cyls);
            memfree(heads);
            memfree(nos);
            memfree(marks);
            memfree(dat);
        }
    }
}

struct container container_jv3 = {
    .init = dsk_init,
    .open = jv3_open,
    .close = jv3_close,
    .write_raw = dsk_write_raw
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
