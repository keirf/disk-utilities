/*
 * stream/caps_disabled.c
 * 
 * Stub CAPS/IPF handler used when library support is disabled at compile time.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include <private/stream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define w(f, a...) fprintf(stderr, "*** " f, ## a)

static struct stream *caps_open(const char *name, unsigned int data_rpm)
{
    int fd;
    char sig[4];

    /* Simple signature check */
    if ((fd = file_open(name, O_RDONLY)) == -1)
        return NULL;
    read_exact(fd, sig, 4);
    close(fd);
    if (strncmp(sig, "CAPS", 4))
        return NULL;

    w("CAPS/IPF image is detected, but support is not enabled.\n");
    w("Support must be enabled at compile time (e.g., 'caps=y make')\n");
    w("Download the library at http://www.softpres.org/download\n");
    w("Respect the SPS Freeware License Agreement!\n");

    return NULL;
}

struct stream_type caps = {
    .open = caps_open,
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
