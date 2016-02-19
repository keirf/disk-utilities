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

/* Todo: Move to a run-time option */
#ifndef JV3_DEBUG
#define JV3_DEBUG 0
#endif

/* Support various levels of debugging information */
static int jv3_debug = JV3_DEBUG;

#define JV3_LOG(l, f, a...) do {                \
    if (jv3_debug >= (l)) printf(f, ##a);       \
} while (0)

/* JV3 warning - always enabled */
#define JV3_WARN(f, a...) JV3_LOG(0, f, ##a)
/* JV3 debugging info */
#define JV3_INFO(f, a...) JV3_LOG(1, f, ##a)
/* JV3 verbose debugging info */
#define JV3_TRACE(f, a...) JV3_LOG(2, f, ##a)

/* ===============================================================
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

static struct container *jv3_open(struct disk *d)
{
    /* not supported */
    return NULL;
};

/* Convert address mark and density to JV3 flags */
static unsigned int mark_to_jv3_flags(uint8_t mark, unsigned int density)
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

/* Convert size to JV3 sector size */
static unsigned int size_to_jv3_flags(int size)
{
    int flags = 0;
    /* Check for non-standard sector size */
    /* Default is 256 bytes */
    switch(size) {
    case 128:
        flags |= 1;
        break;
    case 256:
        break;
    case 512:
        flags |= 3;
        break;
    case 1024:
        flags |= 2;
        break;
    default:
        flags |= 0x100;    /* Size error */
        break;
    }
    return flags;
}


/* Convert track type to JV3 density */
unsigned int type_to_density(int type)
{
    int density = 0;
    /* Bit-rate testing borrowed directly from imd.c */
    switch (type) {
    case TRKTYP_trs80_fm_sd:
        density = 0;
        break;
    case TRKTYP_trs80_mfm_dd:
        density = 1;
        break;
    case TRKTYP_ibm_fm_sd:
        density = 0;
        break;
    case TRKTYP_ibm_fm_dd:
        density = 0;
        break;
    case TRKTYP_ibm_mfm_dd:
        density = 1;
        break;
    case TRKTYP_ibm_mfm_hd:
        density = 1;
        break;
    case TRKTYP_unformatted:
        density = 0xff;
        break;
    default:
        density = 0xff;
        break;
    }
    return density;
}



/* Write out the JV3 header and raw sector dump */
static void jv3_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct track_info *ti;
    uint8_t *secs, *cyls, *heads, *nos, *marks, *dat;
    int track,cylinder,head,sector,sec_sz,mark;
    unsigned int flags;
    unsigned int density;
    unsigned int jv3_state;
    int jv3_ind = 0;
    int side_0_40, side_0_80;
    int side_1_40, side_1_80;
    int single_40, single_80;
    int single, track_80;

    /* truncate to 0 bytes */
    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    /* Guess if we are single sided
     * Test for double step (80 track drive reading 40 track disk) */
    side_0_40 = 0;
    side_1_40 = 0;
    side_0_80 = 0;
    side_1_80 = 0;

    /* Determine disk layout first 
     * Are we reading a 40 track disk with an 80 track drive ? 
     * We use these tests to reject sectors on unused tracks that have
     *     detected some data via cross talk.
     *
     * Truth table:
     * Track counts 0 to > 160, odd numbers are always head 1, even head 0
     * track 80 track           40 track
     * 0 = track 0 head 0    track 0 head 0
     * 1 = track 0 head 1    track 0 head 1
     * 2 = track 1 head    0    track x    x
     * 3 = track 1 head    1    track x    x
     * 4 = track 2 head    0    track 1 head 0
     * 5 = track 2 head    1    track 1 head 1
     */
    for (track = 0; track < di->nr_tracks; track++) {
        ti = &di->track[track];

        /* Skip unformatted or zero-sector tracks */
        if ((ti->type == TRKTYP_unformatted)
            || (ti->type == 0xff)
            || !ti->nr_sectors)
            continue;

        /* Double-step, 40-track tests */
        if ((track & 2) == 0) {
            if (track & 1)
                side_1_40++;
            else
                side_0_40++;
        }

        /* Single-step, 80-track tests */
        if (track & 1)
            side_1_80++;
        else
            side_0_80++;
    }

    /* Single-side 40 track ? */
    single_40 = 0;
    if (side_1_40 < side_0_40/2)
        single_40 = 1;

    /* Single-side 80 track ? */
    single_80 = 0;
    if (side_1_80 < side_0_80/2)
        single_80 = 1;

    track_80 = 0;
    /* Are we reading a 40 track disk with an 80 track drive ? */
    track_80 = (side_0_80 > (side_0_40 * 3 / 2));

    /* Single-sided overall? */
    single = 0;
    if (single_40 || single_80)
        single = 1;

    JV3_INFO("JV3: Cylinders: %d, track_80:%d, single:%d\n", 
             di->nr_tracks, track_80, single);
    JV3_INFO("JV3: side_0_40:%d, side_1_40:%d, side_0_80:%d, side_1_80:%d\n",
             side_0_40, side_1_40, side_0_80, side_1_80);

    /* Loop twice - first pass dumps headers - second pass secors
     * Fill jv3_buf in jv3_state 0
     * Write jv3_but at start of jv3_state 1 - then write sectors */
    for (jv3_state = 0; jv3_state < 2; jv3_state++) {
        /* Are we done with the header? */
        if (jv3_state == 1 && jv3_ind) {
            /* PAD and WRITE PROTECT BYTE */
            while(jv3_ind < JV3_HEADER_SIZE)
                jv3_buf[jv3_ind++] = 0xff;
            write_exact(d->fd, jv3_buf, JV3_HEADER_SIZE);
            jv3_ind = 0; /* ignore header now it's written */
        }

        /* Look at the imd.c container for data access examples */

        /* Process tracks */
        for (track = 0; track < di->nr_tracks; track++) {
            ti = &di->track[track];

            /* TRS80 Denity is 0 (FM) or 1 (MFM)*/
            density = type_to_density(ti->type);

            /* Other bits are errors */
            if (density & 0xfe) {
                /* only warn once */
                if (jv3_state == 0)
                    JV3_WARN("T%u.%u: Ignoring track format '%s' "
                             "while writing JV3 file\n",
                             cyl(track), hd(track), ti->typename);
                continue;    /* skip this unknown track type */
            }
            
            if (!ti->nr_sectors)    /* No sectors ? */
                continue;
    
            /* Invalid sector count ? */
            if ((uint32_t)ti->nr_sectors >= 256) {
                /* only warn once */
                if (jv3_state == 0)
                    JV3_WARN("T%u.%u: Unexpected number of IBM sectors (%u)\n",
                             cyl(track), hd(track), ti->nr_sectors);
                continue;
            }

            /* If the disk is single side and we are on the wrong side */
            if (single && hd(track))
                continue;

            /* 40 tracks ? */
            /* Avoid partial cross-talk reads when using 80 track drive */
            if (!track_80 && (track & 2))
                continue; 
    
            /* This pulls information from the helper. In the case of jV3 we
             * borrow from the ibm.c functions with extra trs80 address marks.
             * The source can be from several file types - only tested with
             * KroFlux CT type. */

            retrieve_ibm_mfm_track(
                d, track, &secs, &cyls, &heads, &nos, &marks, &dat);

            /* FIXME: use track_80, single, etc to match with logical mapping */

            /* From track header */
            cylinder = cyl(track);
            head = hd(track);
            sec_sz = (128u << nos[0]);

            /* More detailed debug level ? */
            if (jv3_state == 0)
                JV3_TRACE("JV3: trk:%3d, secs:%3d, sec_sz:%3d, density:%3d\n",
                          track, ti->nr_sectors, sec_sz, density);
    
            /* Make sure each sector matches properties in the track header
               Constant size, matching head, matching cylinder, etc */
            for (sector = 0; sector < ti->nr_sectors; sector++) {
                /* More detailed debug level ? */
                if (jv3_state == 0)
                    JV3_TRACE("\tcyl:%3d, head:%3d, sector:%3d, "
                              "size:%3d, mark:%02x\n", 
                              cyls[sector], heads[sector], secs[sector], 
                              nos[sector], marks[sector]);

                if ((128u << nos[sector]) != sec_sz) {
                    if (jv3_state == 0)
                        JV3_INFO("T%u.%u: Cannot write mixed-sized sectors "
                                 "to JV3 file\n", cyl(track), hd(track));
                    continue;
                }
                
                if (cyl(cyls[sector]) != cylinder)
                    head |= 0x80;
                if (hd(heads[sector]) != (head&1))
                    head |= 0x40;
            }

            /* Write headers , then data */
            for (sector = 0; sector < ti->nr_sectors; sector++) {
                /* Write JV3 headers first at state 0 */
                if (jv3_state == 1) {
                    /* We assume a fixed size sector */
                    /* TODO double check how the dat[] array is filled */
                    write_exact(d->fd, &dat[sector * sec_sz], sec_sz);
                } else { /* jv3_state == 0, build headers */
                    if (jv3_ind >= JV3_HEADER_SIZE) { /* Fatal Error */
                        JV3_WARN("JV3: header index exceeded:%04x\n",jv3_ind);
                        JV3_WARN("JV3: track:%d,side:%d,sector:%d\n", 
                                 cyls[sector],heads[sector] & 1,sector);
                        errx(1,"\n");
                    }

                    /* address marks */
                    mark = marks[sector];
                    /* map cylinder */
                    jv3_buf[jv3_ind++] = cyls[sector];
                    /* map sector number */
                    jv3_buf[jv3_ind++] = secs[sector];

                    /* JV3 flags */
                    /* Convert sector size to JV3 flags */
                    flags = size_to_jv3_flags(128u << nos[sector]);
                    if (flags & 0x100)
                        JV3_TRACE("JV3: cly:%d, hd:%d, sec(%d), size:%d "
                                  "is INVALID\n",
                                  cyl(track), hd(track), sector,
                                  128u << nos[sector]);
                    /* Convert address mark and density into JV3 flags */
                    flags |=  mark_to_jv3_flags(mark, density);
                    /* Encode side */
                    flags |= (heads[sector] & 1) ? JV3_SIDE : 0;
                    /* Encode Density */
                    flags |= density ? JV3_DENSITY : 0;

                    /* Discard any return status warnings */
                    jv3_buf[jv3_ind++] = flags & 0xff;
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
