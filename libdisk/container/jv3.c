/* libdisk/container/jv3.c
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
 *        disk-analyse -v  -f trs80 disk_trs80_sd.ct disk_trs80_sd.jv3 */

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

#define JV3_LOG(l, f, a...) do {                    \
    if (jv3_debug >= (l)) fprintf(stderr, f, ##a);  \
} while (0)

/* JV3 warning - always enabled */
#define JV3_WARN(f, a...) JV3_LOG(0, f, ##a)
/* JV3 debugging info */
#define JV3_INFO(f, a...) JV3_LOG(1, f, ##a)
/* JV3 verbose debugging info */
#define JV3_TRACE(f, a...) JV3_LOG(2, f, ##a)

/* ===============================================================
 * JV3 Documenation from http://www.tim-mann.org/trs80/dskspec.html
 * Tim Mann http://tim-mann.org 
 *
 * Layout of JV3
 * Note: we only are doing 80/40 track single/double density trsdos/ldos disks
 * (We do not need the double header option for this)
 *
 * typedef struct {
 *      unsigned char track;
 *      unsigned char sector;
 *      unsigned char flags;
 * } SectorHeader;
 *  
 * typedef struct {
 *      SectorHeader sh[JV3_ENTRIES];
 *      unsigned char writeprot;
 * } JV3; */

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

static unsigned char jv3_buf[JV3_HEADER_SIZE+3]; /* add a few bytes of overflow */

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
        flags |= 0x80;
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
    case TRKTYP_ibm_fm_sd:
    case TRKTYP_ibm_fm_sd_recovery:
    case TRKTYP_ibm_fm_dd:
        density = 0;
        break;
    case TRKTYP_ibm_mfm_dd:
    case TRKTYP_ibm_mfm_dd_recovery:
    case TRKTYP_ibm_mfm_hd:
        density = 1;
        break;
    case TRKTYP_unformatted:
        density = 0xff;
        break;
    default:
        density = 0xffff;
        break;
    }
    return density;
}


/* Convert track type to encoding */
char *type_to_encoding(int type)
{
    char *ptr = "INVALID";
    /* Bit-rate testing borrowed directly from imd.c */
    switch (type) {
    case TRKTYP_ibm_fm_sd:
    case TRKTYP_ibm_fm_sd_recovery:
    case TRKTYP_ibm_fm_dd:
        ptr = "FM";
        break;
    case TRKTYP_ibm_mfm_dd:
    case TRKTYP_ibm_mfm_dd_recovery:
    case TRKTYP_ibm_mfm_hd:
        ptr = "MFM";
        break;
    case TRKTYP_unformatted:
        ptr = "UNFORMATTED";
        break;
    default:
        break;
    }
    return ptr;
}

/**
   @brief TEST bit in byte array
   @param[in] *ptr: byte array
   @param[in] off: bit offset
   @return  1 if bit is set, 0 if not
*/
static uint8_t  bittest(uint8_t *ptr, int off)
{
    return( (ptr[off>>3] & (0x80 >> (off&7))) ? 1 : 0 );
}

/**
   @brief SET bit in byte array
   @param[in] *ptr: byte array
   @param[in] off: bit offset
   @return  void
*/
static void bitset(unsigned char *ptr, int off)
{

    ptr[off>>3] |= (0x80 >> (off & 7));
}

/**
   @brief CLEAR bit in byte array
   @param[in] *ptr: byte array
   @param[in] off: bit offset
   @return  void
*/
static void bitclr(unsigned char *ptr, int off )
{

    ptr[off>>3] &= ~(0x80 >> (off & 7));
}


/* ===================================================== */
/* ===================================================== */
/* ===================================================== */


/* Analyse disk information based on physical values */

#define MAX_CYLINDERS 256
#define MAX_SECTORS 256
#define MAX_SIDES 2

#define MAX_TRACKS (MAX_CYLINDERS * MAX_SIDES)



/* information from all tracks and sectors */
typedef struct {
    uint8_t     track[((MAX_TRACKS+7)>>3) ];
    uint8_t     reject_track[((MAX_TRACKS+7)>>3) ];
    uint8_t     cyl[ ((MAX_CYLINDERS+7)>>3) ];
    uint8_t     sec[ ((MAX_SECTORS+7)>>3) ];
    uint8_t        side_40;
    uint8_t        side_80;
    uint8_t        tracks;
    int            test_encoding;
    int            test_density;
    int            test_size;
    int            test_first_cylinder;
    int            test_cylinders;
    int            test_first_sector;
    int            test_sectors;
    int         first;
    int         size;
    int         sectors;
    unsigned int density;
    int         reject_side;
} all_t;

static all_t all[MAX_SIDES];

static void init_trs80_used()
{
    int i,j;

    for(j=0;j<MAX_SIDES;++j) {
        for(i=0;i<MAX_TRACKS;++i)
            bitclr(all[j].track,i);
        for(i=0;i<MAX_TRACKS;++i)
            bitclr(all[j].reject_track,i);
        for(i=0;i<MAX_CYLINDERS;++i)
            bitclr(all[j].cyl,i);
        for(i=0;i<MAX_SECTORS;++i)
            bitclr(all[j].sec,i);
        all[j].tracks = 0;
        all[j].first = 0;
        all[j].sectors = 0;
        all[j].size = -1;
        all[j].density = -1;
        all[j].tracks = 0;
        all[j].side_80 = 0;
        all[j].side_40 = 0;
        all[j].reject_side= 0;

        /* FIXME just these value rater then autodetect */
        all[j].test_encoding = -1;
        all[j].test_density = -1;
        all[j].test_size = -1;
        all[j].test_first_sector = -1;
        all[j].test_sectors = -1;
        all[j].test_first_cylinder = -1;
        all[j].test_cylinders = -1;
        
    }
}

static int realcyl(int track, int eighty)
{
    return( eighty ? cyl(track) : cyl(track) / 2);
}


/* Write out the JV3 header and raw sector dump */
static void jv3_close(struct disk *d)
{
    struct disk_info *di = d->di;
    struct track_info *ti;

    uint8_t *secs, *cyls, *heads, *nos, *marks, *dat;
    uint16_t *crcs;



    unsigned int density;
    int single, track, sector, size;
    int tracks, reject_track, track_80;

    int count;
    int crc_errors;

    int save_density,save_size,save_first,save_sectors;

    unsigned int jv3_state;
    int jv3_ind;
    long jv3_pos;
    unsigned int flags;

    int i;
    char *zeros;


    /* FIXME int missing; */


    /* Guess if we are single sided
     * Test for double step (80 track drive reading 40 track disk) */

    /* Determine disk layout first 
     * Are we reading a 40 track disk with an 80 track drive ? 
     *
     * We use the tests below to reject_track sectors on unused tracks that may
     *  have detected some data via cross talk.
     *
     * Track counts 0 to > 160, odd numbers are always head 1, even head 0
     * track 80 track           40 track
     *
     * Truth table:
     * 0 = track 0 head 0    track 0 head 0
     * 1 = track 0 head 1    track 0 head 1
     * 2 = track 1 head    0    track x    x
     * 3 = track 1 head    1    track x    x
     * 4 = track 2 head    0    track 1 head 0
     * 5 = track 2 head    1    track 1 head 1
     *
     * macro hd() gives the phyical head of the drive
     *   need not be the same as the logical head
     * macro cyl() gives the phyical cylinder of the drive
     *   need not be the same as the logical cylinder */

    tracks = 0;
    size = 0;
    crc_errors = 0;

    init_trs80_used();

    


/* ============================================================ 
 * Pass 1
 * Using only tracks that a 35 (40) track disk would have on side 0
 * (smallest TRSDOS format)
 * Examine head 0 even tracks - 35 or 40 track formats only 
 *     simple test ((track & 3) == 0)   we can step by tracks += 4
 * If we wanted both heads
 *     simple test ((track & 2) == 0)   we can step by tracks += 2
 *
 * On each track; (in the first 35 physical tracks)
 *     Find the lowest numbered sector number found on side 0
 *     Find the sector size - this should NOT change 
 *     Find the number of sectors - this should NOT change - pick largest value
 *
 * FIXME - we may want to treat track 0 as special
 * Later we use this data to verify the remaining sides and tracks
 * ============================================================ */

/* Below we do tests to reject the entire side of a disk
 * Tests for 
 *  density changes
 *  sector count errors
 *  duplicate sectors
 *  cylinder or head mismatch
 *     sector size mismatch or invalid size
 * We keep track of
 *     sector count
 *  density
 *  lowest sector number
 *  all sectors useds */



    /* Examine head 0 even tracks - 35 or 40 track formats only 
     * (track & 2) == 0 */
    for (track = 0; track < di->nr_tracks && (cyl(track)/2) < 35; ++track) 
    {
        /* Skip 80 track tests */
        if (track & 2)
            continue;

        ti = &di->track[track];

        density = type_to_density(ti->type);
        if (track == 0) {
            all[0].density = density;
            all[1].density = density;
            if (density & 0xfffe) {
                JV3_WARN("JV3: bad density on track 0 - bad disk\n");
                exit(1);
            }
        }


        /* unknown track type ???? 
         * Reject all other density tracks */
        if (density & 0xfffe) 
            continue;

        if (all[hd(track)].density != density) {
            all[hd(track)].reject_side++;
            continue;
        }

        if (!ti->nr_sectors)
            continue;

        /* Simple reject_track tests are now done 
         * We can not reject mismatches here - crosstalk can happen */

        /* Get sector information from the TRS80 aware ibm.c helper function
         *   - only tested with KroFlux CT and RAW type */

        reject_track = 0;

        retrieve_ibm_mfm_track(
            d, track, &secs, &cyls, &heads, &nos, &marks, &crcs, &dat);

        if (cyl(track) != cyls[0] && cyl(track)/2 != cyls[0]) {
            JV3_INFO("JV3: C%02u.%02u Cylinder mismatch for track(%d)\n",
                     cyls[0], heads[0],
                     cyl(track));
            reject_track++;
        }

        if (hd(track) != heads[0]) {
            JV3_INFO("JV3: C%02u %02u Head mismatch for track(%d)\n",
                     cyls[0], heads[0],
                     hd(track));
            reject_track++;
        }


        for (sector = 0; !reject_track && sector < ti->nr_sectors; sector++) {
            /* Verify against logical and physical parameters */

            /* FIXME - non zero based cylinder counts ??? 
             * Check for Cylinder mismatch, logical vs physical 
             * Check for 80 or 40 track formats 
             * Cylinder mismatch, logical vs physical ? */
            if (cyls[0] != cyls[sector]) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected cylinder (%d)\n",
                         cyls[0], heads[0], secs[sector], 
                         cyls[sector] );
                reject_track++;
                continue;
            }

            /* Head mismatch ? */
            if (heads[0] != heads[sector]) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected head (%d)\n",
                         cyls[0], heads[0], secs[sector], 
                         heads[sector] );
                reject_track++;
                continue;
            } 

            if (nos[0] != nos[sector]) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected size (%d)\n",
                         cyls[0], heads[0], secs[sector], 
                         128u<<nos[sector] );
                reject_track++;
                continue;
            }

            if (all[hd(track)].first > secs[sector])
                all[hd(track)].first = secs[sector];
            bitset(all[hd(track)].sec,secs[sector]);
        }
        if (!reject_track) {
            if (cyl(track) == cyls[0] )
                all[hd(track)].side_80++;
            else if (cyl(track) / 2 == cyls[0])
                all[hd(track)].side_40++;    
            /* The first size we see is the correct one */
            if (all[hd(track)].size == -1)
                all[hd(track)].size = nos[0];

            /* Get maximum sector count for all tracks */
            if (ti->nr_sectors > all[hd(track)].sectors)
                all[hd(track)].sectors = ti->nr_sectors;
        } else {
            bitset(all[hd(track)].reject_track,track);
            all[hd(track)].reject_side++;
        }

        
        memfree(secs);
        memfree(cyls);
        memfree(heads);
        memfree(nos);
        memfree(marks);
        memfree(crcs);
        memfree(dat);
    }

    /* ============================================================ 
     * Determine
     * 80/40 track stepping
     * Sides
     * Density
     * Number of sectors per track
     * First Sector number
     * Sector size
     * ============================================================ */


    track_80 = 0;
    single = 0;
    save_density = all[0].density;
    save_sectors = all[0].sectors;
    save_first = all[0].first;
    save_size = all[0].size;

    uint8_t     used[ ((MAX_SECTORS+7)>>3) ];

    if (size_to_jv3_flags(128u << save_size) & 0x100) {
        JV3_WARN("JV3: size (%d) is not valid for JV3 format\n", 128u << save_size);
        exit(1);
    }

    if (all[0].reject_side) {
        JV3_WARN("JV3: disk side 0 40 track scan rejected disk\n");
        exit(1);
    }

    if (all[1].reject_side) {
        JV3_INFO("JV3: disk side 1 40 track scan rejected side 1\n");
        single = 1;
    }

    /* Verify that sector use is consistant on side 0 */
    count = 0;
    for (i = 0; i < MAX_SECTORS; ++i)
        if (bittest(all[0].sec, i))
            count++;

    if (count != all[0].sectors ) {
        JV3_WARN("JV3: FATAL sector use mismatch - bad disk\n");
        exit(1);
    }

    /* We can only trust 40 track data fully at this point in our testing */
    track_80 = (all[0].side_80 > (all[0].side_40 * 3 / 2));

    if (all[0].sectors != all[1].sectors)
        single = 1;

    if (all[0].side_80 > (all[1].side_80 * 3 / 2))
        single = 1;

    if (all[0].side_40 > (all[1].side_40 * 3 / 2))
        single = 1;

    if (all[0].density != all[1].density)
        single = 1;

    /* compare sector use on each side */
    count = 0;
    for (i = 0; i < MAX_SECTORS; ++i) {
        if (bittest(all[0].sec,i))
            count++;
        if (bittest(all[1].sec,i))
            count--;
    }

    if (count > 0)
        single = 1;

    /* pass 1 information is DEBUG only, real values are on pass 2 */
    ti = &di->track[0];
    JV3_TRACE("\n");
    JV3_TRACE("===============================================\n");
    JV3_TRACE("pass 1\n");
    
    JV3_TRACE("JV3: %s\n", ti->typename);
    JV3_TRACE("JV3: %s Encoding\n", type_to_encoding(ti->type));

    JV3_TRACE("JV3: %d Cylinders scanned\n", cyl(di->nr_tracks));

    JV3_TRACE("JV3: %d track format\n", (track_80 ? 80 : 40));
    JV3_TRACE("JV3: side 0 side_40: %d\n",all[0].side_40);
    JV3_TRACE("JV3: side 1 side_40: %d\n",all[1].side_40);
    JV3_TRACE("JV3: side 0 side_80: %d\n",all[0].side_80);
    JV3_TRACE("JV3: side 1 side_80: %d\n",all[1].side_80);

    JV3_TRACE("JV3: %s Sided disk\n", (single ? "Single" : "Double") );
    JV3_TRACE("JV3: %d Sectors tracks\n", save_sectors);
    JV3_TRACE("JV3: %d First Sector\n", save_first);
    JV3_TRACE("JV3: %d Sectors size\n", 128u << save_size);
    JV3_TRACE("===============================================\n");


    /* ============================================================ 
     * Pass 2
     * Examine ALL tracks now using first pass data as a filter
     * We know:
     * 80/40 track stepping
     * Sides
     * Density
     * Number of sectors per track
     * First Sector number
     * Sector size
     * ============================================================ */

    tracks = 0;
    count = 0;
    for (track = 0; track < di->nr_tracks; track++) {
        if (single && hd(track))
            continue;

        /* 40 tracks ? */
        if (!track_80 && (track & 2))
            continue;

        ti = &di->track[track];

        density = type_to_density(ti->type);
        if (density & 0xfffe || save_density != density ||
            !ti->nr_sectors || ti->nr_sectors > save_sectors) {
            bitset(all[hd(track)].reject_track,track);
            bitclr(all[hd(track)].track,track);
            continue;
        }

        reject_track = 0;

        retrieve_ibm_mfm_track(
            d, track, &secs, &cyls, &heads, &nos, &marks, &crcs, &dat);

        if (cyl(track) != cyls[0]  && cyl(track)/2 != cyls[0]) {
            JV3_INFO("JV3: C%02u.%02u Cylinder mismatch for track(%d)\n",
                     cyls[0], heads[0],
                     cyl(track));
            reject_track++;
        }
            
        if (hd(track) != heads[0]) {
            JV3_INFO("JV3: C%02u.%02u Head mismatch for track(%d)\n",
                     cyls[0], heads[0],
                     hd(track));
            reject_track++;
        }

        /* size mismatch ? */
        if (save_size != nos[0]) {
            JV3_INFO("JV3: C%02u.%02u Unexpected size (%d)\n",
                     cyls[0],heads[0], 
                     128u<<nos[0]);
            reject_track++;
        }

        if (reject_track) {
            memfree(secs);
            memfree(cyls);
            memfree(heads);
            memfree(nos);
            memfree(marks);
            memfree(crcs);
            memfree(dat);
    
            bitclr(all[hd(track)].track,track);
            bitset(all[hd(track)].reject_track,track);
            continue;
        }

        /* Make sure that properties of each sector matches 
           within the same Physical track and head */

        JV3_TRACE("JV3 DEBUG: C%02u.%02u\n", cyls[0], heads[0]);

        /* used sectors for this track */
        for (i = 0;i < MAX_SECTORS; ++i)
            bitclr(used,i);

        /* Check Logical values for consistancy accross all 
           sectors in this physical track */

        for (sector = 0; !reject_track && sector < ti->nr_sectors; sector++) {
            /* Cylinder mismatch, logical vs physical ? */
            if( cyls[0] != cyls[sector] ) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected cylinder (%d)\n",
                         cyls[0], heads[0], secs[sector], 
                         cyls[sector] );
                reject_track++;
                break;
            }

            /* Head mismatch ? */
            if (heads[0] != heads[sector]) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected head (%d)\n",
                         cyls[0], heads[0], secs[sector], 
                         heads[sector] );
                reject_track++;
                break;
            } 

            /* Is sector valid based information from first 35 sectors */
            if (!bittest(all[hd(track)].sec,secs[sector])) {
                JV3_INFO("JV3: C%02u.%02u.%02u Unexpected Sector (%d)\n",
                         realcyl(track,track_80), hd(track), secs[sector], 
                         secs[sector] );
                ++reject_track;
                break;
            }

            /* Is this sector duplicated ?*/
            if (bittest(used,secs[sector])) {
                JV3_INFO("JV3: C%02u.%02u.%02u.Duplicate Sector (%d)\n",
                         realcyl(track,track_80), hd(track), secs[sector], 
                         secs[sector] );
                ++reject_track;
                break;
            }


            /* size mismatch ? */
            if (nos[0] != nos[sector]) {
                JV3_INFO("JV3: C%02u.%02u.%02u.Unexpected size (%d)\n",
                         realcyl(track,track_80), hd(track), secs[sector], 
                         128u<<nos[sector] );
                reject_track++;
                break;
            }


            if (!reject_track) {
                /* Track CRC Errors */
                if(crcs[sector]) {
                    JV3_WARN("JV3: C%02u.%02u.%02u CRC(%4x) error\n",
                             cyls[sector],heads[sector], secs[sector], crcs[sector]);
                    ++crc_errors;
                    /* crc errors are not fatal */
                }
                JV3_TRACE("JV3 DEBUG: C%02u.%02u.%02u: mark:%02x\n",
                          cyls[0], heads[0], secs[sector], marks[sector]);
                bitset(used, secs[sector]);
            }
        }    /* for(sector=0 ....) */

        if (!reject_track) {
            bitclr(all[hd(track)].reject_track,track);
            bitset(all[hd(track)].track,track);
            bitset(all[hd(track)].cyl, cyls[0] );
            /* save cylinder attributes */
            /* Save density for this track */
            if (hd(track) == 0)
                ++tracks;
        }

        if (reject_track) {
            bitset(all[hd(track)].reject_track,track);
            bitclr(all[hd(track)].track,track);
            JV3_INFO("JV3: T%u.%u track rejected\n",
                     cyl(track), hd(track));
        }

        /* Free data from retrieve() */
        memfree(secs);
        memfree(cyls);
        memfree(heads);
        memfree(nos);
        memfree(marks);
        memfree(crcs);
        memfree(dat);
    } /* for (track = 0; track < di->nr_tracks; track++) */
    /* ============================================================ */

    for (track = 0; track < di->nr_tracks; track++) {
        if(single && hd(track))
            continue;

        /* 40 tracks ? */
        if (!track_80 && (track & 2))
            continue; 
    
        if (realcyl(track,track_80) >= tracks)
            break;

        /* FIXME */
        if (!bittest(all[hd(track)].track,track)) {
            JV3_TRACE("DEBUG: track:%d.%d bad\n",
                      realcyl(track,track_80) , hd(track));
        }

        if (bittest(all[hd(track)].reject_track,track)) {
            JV3_TRACE("DEBUG: track:%d.%d reject\n",
                      realcyl(track,track_80) , hd(track));
        }
    }

    /* Test for total tracks */

    ti = &di->track[0];
    JV3_TRACE("\n");
    JV3_TRACE("===============================================\n");
    JV3_TRACE("pass 2\n");
    JV3_INFO("JV3: %s\n", ti->typename);
    JV3_INFO("JV3: %s Encoding\n", type_to_encoding(ti->type));
    JV3_INFO("JV3: %d Cylinders scanned\n", di->nr_tracks / 2 );
    JV3_INFO("JV3: %s Sided disk\n", (single ? "Single" : "Double") );
    JV3_INFO("JV3: %d track format\n", (track_80 ? 80 : 40));
    JV3_INFO("JV3: %d Tracks used\n", tracks);
    JV3_INFO("JV3: %d Sectors tracks\n", save_sectors);
    JV3_INFO("JV3: %d First Sector\n", save_first);
    JV3_INFO("JV3: %d Sectors size\n", 128u << save_size);
    if(crc_errors)
        JV3_INFO("JV3: %d CRC errors\n", crc_errors);
    JV3_TRACE("===============================================\n");

    
    /* =========================================================== */

    /* truncate to 0 bytes */
    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);


    /* PAD and WRITE PROTECT BYTE */
    jv3_ind = 0;
    while (jv3_ind < JV3_HEADER_SIZE - 3) {
        jv3_buf[jv3_ind++] = JV3_FREE;  /* CYL */
        jv3_buf[jv3_ind++] = JV3_FREE;  /* SECTOR */
        jv3_buf[jv3_ind++] = JV3_FREEF; /* FLAGS */
    }
                
    /* Loop twice - first pass dumps headers - second pass sectors
     * Fill jv3_buf in jv3_state 0
     * Write jv3_buf at state 1, data at state 2
     */

    /*
     * FIXME FIXME
     * We should look trough the trs80 structure NOT THIS
     */

    count = 128u << 7;

    zeros = calloc(count,1);

    if (!zeros) {
        JV3_WARN("JV3: memory calloc failed\n");
        exit(1);
    }

    jv3_pos = 0;

    for (jv3_state = 0; jv3_state < 3; jv3_state++) {
        if (jv3_state == 0 ) {
            jv3_pos = 0;
            jv3_ind = 0;
        }
        /* Are we done with the header? */
        if (jv3_state == 1) {
            lseek(d->fd, 0, SEEK_SET);
            write_exact(d->fd, jv3_buf, JV3_HEADER_SIZE);
            continue;
        }
        if (jv3_state == 2) {
            lseek(d->fd, ((long) JV3_HEADER_SIZE),  SEEK_SET);
            jv3_pos = JV3_HEADER_SIZE;
            jv3_ind = 0;
        }

        /* Look at the imd.c container for data access examples */

        /* Reset sector count, first sector, size etc fro invalid tracks */
        jv3_ind = 0;
        for (track = 0; track < di->nr_tracks; track++) {

            if (single && hd(track))
                continue;

            /* 40 tracks ? */
            if (!track_80 && (track & 2))
                continue;

            if (realcyl(track,track_80) >= tracks)
                break;

            /* FIXME */
            if (!bittest(all[hd(track)].track,track))
                continue;

            if (bittest(all[hd(track)].reject_track,track))
                continue;

            ti = &di->track[track];

            if (!ti->nr_sectors) {
                JV3_WARN("JV3: T%u.%u: FATAL expected (%d) sectors got ZERO\n",
                         cyl(track), hd(track), save_sectors);
                exit(1);
            }


            retrieve_ibm_mfm_track(
                d, track, &secs, &cyls, &heads, &nos, &marks, &crcs, &dat);

            size = 128u << save_size;

            for (sector = 0; sector < save_sectors; ++sector) {

                /* Write sectors in state 0 */
                if (jv3_state == 2) {
                    if (ti->nr_sectors <= sector) {
                        JV3_TRACE("JV3: DEBUG: C%02u.%02u.%02u: mark:%02x, pos:%ld FILL\n", 
                                  cyls[0],
                                  heads[0],
                                  0xff,
                                  marks[0],
                                  jv3_pos);
                        write_exact(d->fd, zeros, size);
                    } else {
                        JV3_TRACE("DEBUG: C%02u.%02u.%02u: mark:%02x, pos:%ld\n", 
                                  cyls[sector],
                                  heads[sector],
                                  secs[sector],
                                  marks[sector],
                                  jv3_pos);
                        /* data */
                        write_exact(d->fd, &dat[sector * size], size);
                    }
                    jv3_pos += size;

                } /* if(jv3_state == 2) */


                /* Write headers in state 0 */
                if (jv3_state == 0) {
                    if (jv3_ind >= ((JV3_ENTRIES) - 1)) {
                        JV3_WARN("JV3: header index exceeded:%04x\n",jv3_ind);
                        exit(1);
                    }

                    /* Convert sector size to JV3 flags */

                    if (ti->nr_sectors <= sector) {
                        JV3_TRACE("DEBUG: C%02u.%u %u: mark:%02x, "
                                  "ind:%d FILL\n", 
                                  cyls[0],
                                  heads[0],
                                  0xff,
                                  marks[0],
                                  jv3_ind);
                        /* JV3 flags */
                        flags = size_to_jv3_flags(size);

                        /* Convert address mark and density to JV3 flags */
                        flags |=  mark_to_jv3_flags(marks[0], save_density);
                        /* Encode side */
                        flags |= heads[0] ? JV3_SIDE : 0;

#ifdef JV3_CRC
                        flags |= JV3_ERROR;
#endif
                        /* map cylinder */
                        jv3_buf[jv3_ind*3] = cyls[0];
                        /* map sector number */

                        /* FIXME - fill in missing with bit mask */
                        jv3_buf[jv3_ind*3+1] = 0xff;

                        jv3_buf[jv3_ind*3+2] = flags & 0xff;
                        jv3_ind++;

                    } else {

                        JV3_TRACE("DEBUG: C%02u.%02u.%02u: mark:%02x, "
                                  "ind:%d\n", 
                                  cyls[sector],
                                  heads[sector],
                                  secs[sector],
                                  marks[sector],
                                  jv3_ind);

                        /* Error testing has already been done */
                        flags = size_to_jv3_flags(size);

                        /* Convert address mark and density into JV3 flags */
                        flags |= mark_to_jv3_flags(marks[sector], save_density);
                        /* Encode side */
                        flags |= heads[sector] ? JV3_SIDE : 0;

                        /* Encode CRC Error */
                        if (crcs[sector]) {
#ifdef JV3_CRC_HIDE
                            flags |= JV3_ERROR;
#endif
                        }

                        /* map cylinder */
                        jv3_buf[jv3_ind*3] = cyls[sector];
                        /* map sector number */
                        jv3_buf[jv3_ind*3+1] = secs[sector];
                        jv3_buf[jv3_ind*3+2] = flags & 0xff;
                        jv3_ind++;
                    }

                }    /* if(jv3_state == 0) */
            } /* for(sector ...) */
            /* Free data from retrieve() */
            memfree(secs);
            memfree(cyls);
            memfree(heads);
            memfree(nos);
            memfree(marks);
            memfree(crcs);
            memfree(dat);
        } /* for (track = 0; track < di->nr_tracks; track++) */
    } /* for(jv3_state ..) */
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
