/******************************************************************************
 * stream/caps.c
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <caps/capsimage.h>
#include <dlfcn.h>

#define CAPSLIB_NAME    "libcapsimage.so.2"

#define CAPS_FLAGS (DI_LOCK_DENVAR|DI_LOCK_DENNOISE|DI_LOCK_NOISE|      \
                    DI_LOCK_UPDATEFD|DI_LOCK_TYPE)

struct caps_stream {
    struct stream s;
    CapsLong container;

    /* Current track info */
    unsigned int track, rev;
    uint8_t *mfm;
    uint16_t *speed;
    uint32_t pos, bitlen, ns_per_cell;
    struct CapsTrackInfo ti;
};

static struct {
    unsigned int ref;
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

static int get_capslib(void)
{
    if (capslib.ref++)
        return 1;

    if ((capslib.handle = dlopen(CAPSLIB_NAME, RTLD_LAZY)) == NULL)
        goto fail_no_handle;

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
    warnx("Unable to open " CAPSLIB_NAME);
    w("Download the library at http://www.softpres.org/download\n");
    w("Respect the SPS Freeware License Agreement!\n");
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

static struct stream *caps_open(const char *name)
{
    int fd;
    char sig[4];
    struct caps_stream *cpss;

    /* Simple signature check */
    if ((fd = open(name, O_RDONLY)) == -1)
        return NULL;
    read_exact(fd, sig, 4);
    close(fd);
    if (strncmp(sig, "CAPS", 4))
        return NULL;

    if (!get_capslib())
        return NULL;

    cpss = memalloc(sizeof(*cpss));
    cpss->track = ~0u;

    if ((cpss->container = CAPSAddImage()) < 0) {
        warnx("caps: Could not create image container");
        goto fail1;
    }

    if (CAPSLockImage(cpss->container, name) != imgeOk) {
        warnx("caps: Could not load image into container");
        goto fail2;
    }

    if (CAPSLoadImage (cpss->container, CAPS_FLAGS) != imgeOk) {
        warnx("caps: Could not prefetch image data");
        goto fail3;
    }

    return &cpss->s;

fail3:
    CAPSUnlockImage(cpss->container);
fail2:
    CAPSRemImage(cpss->container);
fail1:
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
    memfree(cpss);
    put_capslib();
}

static void caps_nextrevolution(struct stream *s)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);

    cpss->rev = (cpss->rev + 1) % cpss->ti.trackcnt;
    cpss->mfm = cpss->ti.trackdata[cpss->rev];
    cpss->bitlen = cpss->ti.tracksize[cpss->rev] * 8;
    cpss->pos = 0;
    cpss->ns_per_cell = 200000000u / cpss->bitlen;
    index_reset(s);
}

static void caps_reset(struct stream *s, unsigned int tracknr)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);
    unsigned int i;

    if (cpss->track != tracknr) {
        memfree(cpss->speed);
        cpss->speed = NULL;
        memset(&cpss->ti, 0, sizeof(cpss->ti));
        CAPSLockTrack(&cpss->ti, cpss->container,
                      tracknr / 2, tracknr & 1, CAPS_FLAGS);
        cpss->track = tracknr;
        if (cpss->ti.timelen) {
            cpss->speed = memalloc(cpss->ti.timelen * sizeof(uint16_t));
            for (i = 0; i < cpss->ti.timelen; i++)
                cpss->speed[i] = cpss->ti.timebuf[i];
        }
    }

    cpss->rev = cpss->ti.trackcnt - 1;
    caps_nextrevolution(s);
}

static int caps_next_bit(struct stream *s)
{
    struct caps_stream *cpss = container_of(s, struct caps_stream, s);
    uint16_t speed;
    uint8_t dat;

    if (++cpss->pos >= cpss->bitlen)
        caps_nextrevolution(s);

    dat = !!(cpss->mfm[cpss->pos >> 3] & (0x80u >> (cpss->pos & 7)));
    speed = ((cpss->pos >> 3) < cpss->ti.timelen)
        ? cpss->speed[cpss->pos >> 3] : 1000u;
    s->latency += (cpss->ns_per_cell * speed) / 1000u;

    return dat;
}

struct stream_type caps = {
    .open = caps_open,
    .close = caps_close,
    .reset = caps_reset,
    .next_bit = caps_next_bit
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
