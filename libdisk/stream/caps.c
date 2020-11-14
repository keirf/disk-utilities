/*
 * stream/caps.c
 */

#include <libdisk/util.h>
#include <private/stream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <caps/capsimage.h>
#include <dlfcn.h>

#ifdef __APPLE__
#define CAPSLIB_NAME    "/Library/Frameworks/CAPSImage.framework/CAPSImage"
#else
#define CAPSLIB_NAME    "libcapsimage.so"
#endif

#define CAPS_FLAGS (DI_LOCK_DENVAR|DI_LOCK_DENNOISE|DI_LOCK_NOISE|      \
                    DI_LOCK_UPDATEFD|DI_LOCK_TYPE)

struct caps_stream {
    struct stream s;
    CapsLong container;
    bool_t is_ipf;

    /* Current track info */
    unsigned int track;
    uint8_t *bits;
    uint16_t *speed;
    uint32_t pos, bitlen, ns_per_cell;
    struct CapsTrackInfoT1 ti;
};

static struct {
    unsigned int ref, version;
    void *handle;
    CapsLong (*Init)(void);
    CapsLong (*Exit)(void);
    CapsLong (*AddImage)(void);
    CapsLong (*RemImage)(CapsLong id);
    CapsLong (*LockImage)(
        CapsLong id, const char *name);
    CapsLong (*LockImageMemory)(
        CapsLong id, CapsUByte *buffer, CapsULong length, CapsULong flag);
    CapsLong (*UnlockImage)(
        CapsLong id);
    CapsLong (*LoadImage)(
        CapsLong id, CapsULong flag);
    CapsLong (*GetImageInfo)(
        struct CapsImageInfo *pi, CapsLong id);
    CapsLong (*LockTrack)(
        struct CapsTrackInfo *pi, CapsLong id, CapsULong cylinder,
        CapsULong head, CapsULong flag);
    CapsLong (*UnlockTrack)(
        CapsLong id, CapsULong cylinder, CapsULong head);
    CapsLong (*UnlockAllTracks)(
        CapsLong id);
} capslib;

#define CAPSInit            capslib.Init
#define CAPSExit            capslib.Exit
#define CAPSAddImage        capslib.AddImage
#define CAPSRemImage        capslib.RemImage
#define CAPSLockImage       capslib.LockImage
#define CAPSLockImageMemory capslib.LockImageMemory
#define CAPSUnlockImage     capslib.UnlockImage
#define CAPSLoadImage       capslib.LoadImage
#define CAPSGetImageInfo    capslib.GetImageInfo
#define CAPSLockTrack       capslib.LockTrack
#define CAPSUnlockTrack     capslib.UnlockTrack
#define CAPSUnlockAllTracks capslib.UnlockAllTracks

#define w(f, a...) fprintf(stderr, "*** " f, ## a)

static void print_library_download_info(void)
{
    w("Download the library at http://www.softpres.org/download\n");
    w("Respect the SPS Freeware License Agreement!\n");
    w("See the Disk-Utilities/README for more help.\n");
}

static int get_capslib(void)
{
    if (capslib.ref++)
        return 1;

#ifdef __APPLE__
    if ((capslib.handle = dlopen(CAPSLIB_NAME, RTLD_LAZY)) == NULL) {
        warnx("Unable to open " CAPSLIB_NAME);
        goto fail_no_handle;
    }
    capslib.version = 5; /* guess */
#else
    if ((capslib.handle = dlopen(CAPSLIB_NAME ".5", RTLD_LAZY))) {
        capslib.version = 5;
    } else if ((capslib.handle = dlopen(CAPSLIB_NAME ".4", RTLD_LAZY))) {
        capslib.version = 4;
    } else {
        warnx("Unable to open " CAPSLIB_NAME ".5 or " CAPSLIB_NAME ".4");
        goto fail_no_handle;
    }
#endif

#define GETSYM(sym)                                     \
    capslib.sym = dlsym(capslib.handle, "CAPS"#sym);    \
    if (dlerror() != 0) goto fail;
    GETSYM(Init);
    GETSYM(Exit);
    GETSYM(AddImage);
    GETSYM(RemImage);
    GETSYM(LockImage);
    GETSYM(LockImageMemory);
    GETSYM(UnlockImage);
    GETSYM(LoadImage);
    GETSYM(GetImageInfo);
    GETSYM(LockTrack);
    GETSYM(UnlockTrack);
    GETSYM(UnlockAllTracks);
#undef GETSYM
    if (CAPSInit() != imgeOk)
        goto fail;
    return 1;

fail:
    dlclose(capslib.handle);
fail_no_handle:
    print_library_download_info();
    --capslib.ref;
    return 0;
}

static void put_capslib(void)
{
    if (--capslib.ref)
        return;
    CAPSExit();
    dlclose(capslib.handle);
}

static struct stream *caps_open(const char *name, unsigned int data_rpm)
{
    int fd;
    char sig[4], suffix[8];
    struct caps_stream *cpss;

    /* Simple signature check */
    if ((fd = file_open(name, O_RDONLY)) == -1)
        return NULL;
    read_exact(fd, sig, 4);
    close(fd);
    if (strncmp(sig, "CAPS", 4))
        return NULL;

    if (!get_capslib())
        return NULL;

    cpss = memalloc(sizeof(*cpss));
    cpss->track = ~0u;

    filename_extension(name, suffix, sizeof(suffix));
    cpss->is_ipf = !strcmp(suffix, "ipf");

    if ((cpss->container = CAPSAddImage()) < 0) {
        warnx("caps: Could not create image container");
        goto fail1;
    }

    if (CAPSLockImage(cpss->container, name) != imgeOk) {
        warnx("caps: Could not load image into container");
        goto fail2;
    }

    if (CAPSLoadImage(cpss->container, CAPS_FLAGS) != imgeOk) {
        warnx("caps: Could not prefetch image data");
        goto fail3;
    }

    return &cpss->s;

fail3:
    CAPSUnlockImage(cpss->container);
fail2:
    CAPSRemImage(cpss->container);
fail1:
    if ((capslib.version < 5) && !cpss->is_ipf) {
        w("CT Raw image files require v5+ of the CAPS/SPS library\n");
        print_library_download_info();
    }
    memfree(cpss);
    put_capslib();
        
    return NULL;
}

static void caps_close(struct stream *s)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);
    CAPSUnlockAllTracks(cpss->container);
    CAPSUnlockImage(cpss->container);
    CAPSRemImage(cpss->container);
    memfree(cpss->speed);
    memfree(cpss);
    put_capslib();
}

static int caps_select_track(struct stream *s, unsigned int tracknr)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);
    struct CapsTrackInfoT1 ti;
    unsigned int i;
    int rc;

    /* Attempt to load one track revolution. Modify nothing on failure. */
    memset(&ti, 0, sizeof(ti));
    ti.type = 1;
    rc = CAPSLockTrack((struct CapsTrackInfo *)&ti, cpss->container,
                       cyl(tracknr), hd(tracknr), CAPS_FLAGS);
    if (rc)
        return -1;

    /* Commit new track info. */
    memcpy(&cpss->ti, &ti, sizeof(ti));
    cpss->track = tracknr;

    /* CTRaw dumps get bogus speed info from the CAPS library. 
     * Assume they are uniform density. */
    if (!cpss->is_ipf)
        cpss->ti.timelen = 0;

    /* Commit new speed/density info. */
    memfree(cpss->speed);
    cpss->speed = NULL;
    if (cpss->ti.timelen) {
        cpss->speed = memalloc(cpss->ti.timelen * sizeof(uint16_t));
        for (i = 0; i < cpss->ti.timelen; i++)
            cpss->speed[i] = cpss->ti.timebuf[i];
    }

    return 0;
}

static void caps_reset(struct stream *s)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);

    if (cpss->ti.type & CTIT_FLAG_FLAKEY)
        (void)caps_select_track(s, cpss->track);

    cpss->bits = cpss->ti.trackbuf;
    cpss->bitlen = cpss->ti.tracklen * 8;
    cpss->pos = 0;
    cpss->ns_per_cell = track_nsecs_from_rpm(s->data_rpm) / cpss->bitlen;
}

static int caps_next_flux(struct stream *s)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);
    uint16_t speed;
    uint8_t dat;
    int flux = 0;

    do {
        if (++cpss->pos >= cpss->bitlen) {
            caps_reset(s);
            s->ns_to_index = s->flux + flux;
        }
        dat = !!(cpss->bits[cpss->pos >> 3] & (0x80u >> (cpss->pos & 7)));
        speed = ((cpss->pos >> 3) < cpss->ti.timelen)
            ? cpss->speed[cpss->pos >> 3] : 1000u;
        flux += (cpss->ns_per_cell * speed) / 1000u;
    } while (!dat && (flux < 1000000 /* 1ms */));

    s->flux += flux;
    return 0;
}

struct stream_type caps = {
    .open = caps_open,
    .close = caps_close,
    .select_track = caps_select_track,
    .reset = caps_reset,
    .next_flux = caps_next_flux,
    .suffix = { "ipf", "ct", "ctr", "raw", NULL }
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
