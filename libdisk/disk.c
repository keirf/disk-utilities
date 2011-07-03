/******************************************************************************
 * disk/disk.c
 * 
 * Custom disk layouts -- container format and handlers.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

#define DEFAULT_TRACKS_PER_DISK         160

/* Determined empirically -- larger than expected for 2us bitcell @ 300rpm */
#define DEFAULT_BITS_PER_TRACK       100150

struct disk {
    int fd;
    bool_t read_only;
    enum { DTYP_BAD, DTYP_ADF, DTYP_KFDK } disk_type;
    enum track_type prev_type;
    struct disk_header *dh;
    unsigned char **track_data;
};

extern struct track_handler unformatted_handler;
extern struct track_handler amigados_handler;
extern struct track_handler amigados_labelled_handler;
extern struct track_handler copylock_handler;
extern struct track_handler lemmings_handler;
extern struct track_handler pdos_handler;

const static struct track_handler *handlers[] = {
    &unformatted_handler,
    &amigados_handler,
    &amigados_labelled_handler,
    &copylock_handler,
    &lemmings_handler,
    &pdos_handler,
    NULL
};

const static struct track_handler *write_handlers[] = {
    &unformatted_handler,
    &amigados_handler,
    &copylock_handler,
    &lemmings_handler,
    &pdos_handler,
    NULL
};

/* ADF image helpers */
static void adf_init(struct disk *d);
static struct disk *adf_open(struct disk *d, bool_t quiet);
static void adf_close(struct disk *d);
static void *adf_write_mfm(
    unsigned int tracknr, struct track_header *th, struct stream *s);

static void prep_header_to_disk(struct disk_header *dh)
{
    struct track_header *th;
    unsigned int i, nr_tracks;

    nr_tracks = dh->nr_tracks;
    dh->version = htons(dh->version);
    dh->nr_tracks = htons(dh->nr_tracks);

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        th->type = htons(th->type);
        th->bytes_per_sector = htons(th->bytes_per_sector);
        th->off = htonl(th->off);
        th->len = htonl(th->len);
        th->data_bitoff = htonl(th->data_bitoff);
        th->total_bits = htonl(th->total_bits);
    }
}

static void prep_header_from_disk(struct disk_header *dh)
{
    struct track_header *th;
    unsigned int i, nr_tracks;

    dh->version = ntohs(dh->version);
    dh->nr_tracks = ntohs(dh->nr_tracks);
    nr_tracks = dh->nr_tracks;

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        th->type = ntohs(th->type);
        th->bytes_per_sector = ntohs(th->bytes_per_sector);
        th->off = ntohl(th->off);
        th->len = ntohl(th->len);
        th->data_bitoff = ntohl(th->data_bitoff);
        th->total_bits = ntohl(th->total_bits);
    }
}

static unsigned int type_from_filename(const char *name, bool_t quiet)
{
    const char *p = name + strlen(name) - 4;
    if ( p < name )
        goto fail;
    if ( !strcmp(p, ".adf") )
        return DTYP_ADF;
    if ( !strcmp(p, ".dsk") )
        return DTYP_KFDK;
fail:
    if ( !quiet )
        warnx("Unknown file suffix: %s (valid suffixes: .adf,.dsk)", name);
    return DTYP_BAD;
}

struct disk *disk_create(const char *name)
{
    struct track_header *th;
    struct disk_header *dh;
    struct disk *d;
    unsigned int i, sz, disk_type, nr_tracks = DEFAULT_TRACKS_PER_DISK;
    int fd;

    disk_type = type_from_filename(name, 0);
    if ( disk_type == DTYP_BAD )
        return NULL;

    if ( (fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1 )
    {
        warn("%s", name);
        return NULL;
    }

    sz = sizeof(*d) + sizeof(struct disk_header)
        + nr_tracks*sizeof(unsigned char *)
        + (nr_tracks-1)*sizeof(struct track_header);
    d = memalloc(sz);
    memset(d, 0, sz);

    d->disk_type = disk_type;
    d->fd = fd;
    d->prev_type = TRKTYP_amigados;
    d->track_data = (unsigned char **)(d + 1);
    d->dh = dh = (struct disk_header *)(d->track_data + nr_tracks);

    strncpy(dh->signature, "KFDK", 4);
    dh->version = 0;
    dh->nr_tracks = nr_tracks;

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        th->type = TRKTYP_unformatted;
        th->total_bits = TRK_WEAK;
    }

    if ( disk_type == DTYP_ADF )
        adf_init(d);

    return d;
}

struct disk *disk_open(const char *name, int read_only, int quiet)
{
    struct track_header *th;
    struct disk_header *dh;
    struct disk *d;
    unsigned int i, sz, disk_type, nr_tracks = DEFAULT_TRACKS_PER_DISK;
    int fd;

    disk_type = type_from_filename(name, quiet);
    if ( disk_type == DTYP_BAD )
        return NULL;

    if ( (fd = open(name, read_only ? O_RDONLY : O_RDWR)) == -1 )
    {
        if ( !quiet )
            warn("%s", name);
        return NULL;
    }

    sz = sizeof(*d) + sizeof(struct disk_header)
        + nr_tracks*sizeof(unsigned char *)
        + (nr_tracks-1)*sizeof(struct track_header);
    d = memalloc(sz);
    memset(d, 0, sz);

    d->disk_type = disk_type;
    d->read_only = read_only;
    d->fd = fd;
    d->prev_type = TRKTYP_amigados;
    d->track_data = (unsigned char **)(d + 1);
    d->dh = dh = (struct disk_header *)(d->track_data + nr_tracks);

    if ( disk_type == DTYP_ADF )
        return adf_open(d, quiet);

    read_exact(fd, dh, sizeof(struct disk_header)
               + (nr_tracks-1)*sizeof(struct track_header));
    if ( strncmp(dh->signature, "KFDK", 4) ||
         (ntohs(dh->version) != 0) ||
         (ntohs(dh->nr_tracks) != nr_tracks) )
    {
        if ( !quiet )
            warnx("%s: Bad disk image", name);
        memfree(d);
        return NULL;
    }

    prep_header_from_disk(d->dh);

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        if ( th->len == 0 )
            continue;
        lseek(fd, th->off, SEEK_SET);
        d->track_data[i] = memalloc(th->len);
        read_exact(fd, d->track_data[i], th->len);
    }

    return d;    
}

void disk_close(struct disk *d)
{
    struct track_header *th;
    struct disk_header *dh = d->dh;
    unsigned int i, nr_tracks = dh->nr_tracks, datend, datstrt;

    if ( d->read_only )
        goto free_and_exit;

    lseek(d->fd, 0, SEEK_SET);
    ftruncate(d->fd, 0);

    if ( d->disk_type == DTYP_ADF )
    {
        adf_close(d);
        goto free_and_exit;
    }

    datstrt = datend = sizeof(struct disk_header)
        + (nr_tracks-1)*sizeof(struct track_header);

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        th->off = datend;
        datend += th->len;
    }

    prep_header_to_disk(d->dh);
    write_exact(d->fd, d->dh, datstrt);
    prep_header_from_disk(d->dh);

    for ( i = 0; i < nr_tracks; i++ )
    {
        th = &dh->track[i];
        if ( th->len )
            write_exact(d->fd, d->track_data[i], th->len);
    }

free_and_exit:
    for ( i = 0; i < nr_tracks; i++ )
        memfree(d->track_data[i]);
    close(d->fd);
    memfree(d);
}

struct disk_header *disk_get_header(struct disk *d)
{
    return d->dh;
}

void track_read_mfm(struct disk *d, unsigned int tracknr,
                    uint8_t **mfm, uint16_t **speed, uint32_t *bitlen)
{
    struct disk_header *dh = d->dh;
    struct track_header *th = &dh->track[tracknr];
    const struct track_handler *thnd;
    struct track_buffer tbuf = { 0 };

    thnd = handlers[th->type];
    thnd->read_mfm(tracknr, &tbuf, th, d->track_data[tracknr]);

    *mfm = tbuf.mfm;
    *speed = tbuf.speed;
    *bitlen = tbuf.len;
}

void track_write_mfm_from_stream(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    const struct track_handler *thnd;
    struct disk_header *dh = d->dh;
    struct track_header *th = &dh->track[tracknr];
    int i;
    void *dat = NULL;

    if ( d->track_data[tracknr] )
        memfree(d->track_data[tracknr]);
    d->track_data[tracknr] = NULL;

    if ( d->disk_type == DTYP_ADF )
    {
        dat = adf_write_mfm(tracknr, th, s);
        goto out;
    }

    for ( i = -1; dat == NULL; i++ )
    {
        thnd = (i == -1) ? handlers[d->prev_type] : write_handlers[i];
        if ( thnd == NULL )
            break;
        memset(th, 0, sizeof(*th));
        th->type = thnd->type;
        th->total_bits = DEFAULT_BITS_PER_TRACK;
        stream_reset(s, tracknr);
        stream_next_index(s);
        dat = thnd->write_mfm(tracknr, th, s);
    }

    if ( dat == NULL )
    {
        memset(th, 0, sizeof(*th));
        th->total_bits = TRK_WEAK;
    }
    else
    {
        d->prev_type = th->type;
    }

    if ( th->total_bits == 0 )
    {
        stream_reset(s, tracknr);
        stream_next_index(s);
        stream_next_index(s);
        th->total_bits = s->track_bitlen ? : DEFAULT_BITS_PER_TRACK;
    }

    th->data_bitoff = (int32_t)th->data_bitoff % (int32_t)th->total_bits;
    if ( (int32_t)th->data_bitoff < 0 )
        th->data_bitoff += th->total_bits;

out:
    d->track_data[tracknr] = dat;
}

void track_write_mfm(
    struct disk *d, unsigned int tracknr,
    uint8_t *mfm, uint16_t *speed, uint32_t bitlen)
{
    struct stream *s = stream_soft_open(mfm, speed, bitlen);
    track_write_mfm_from_stream(d, tracknr, s);
    stream_close(s);
}

void track_read_sector(struct disk *d, void *dat)
{
    errx(1, NULL);
}

void track_write_sector(struct disk *d, void *dat)
{
    errx(1, NULL);
}

const char *track_type_name(struct disk *d, unsigned int tracknr)
{
    return handlers[d->dh->track[tracknr].type]->name;
}


/**********************************
 * ADF IMAGE HELPERS
 */

static void *adf_init_track(struct track_header *th)
{
    unsigned int i;
    void *dat;

    th->type = TRKTYP_amigados;
    th->bytes_per_sector = 512;
    th->nr_sectors = 11;
    write_valid_sector_map(th, 0);
    th->len = th->bytes_per_sector * th->nr_sectors;
    th->data_bitoff = 1024;
    th->total_bits = DEFAULT_BITS_PER_TRACK;

    dat = memalloc(th->len);
    for ( i = 0; i < th->len/4; i++ )
        memcpy(dat+i*4, "NDOS", 4);

    return dat;
}

static void adf_init(struct disk *d)
{
    struct track_header *th;
    struct disk_header *dh = d->dh;
    unsigned int i, j;

    dh->nr_tracks = 160;

    for ( i = 0; i < dh->nr_tracks; i++ )
    {
        d->track_data[i] = adf_init_track(&dh->track[i]);
    }
}

static struct disk *adf_open(struct disk *d, bool_t quiet)
{
    struct track_header *th;
    struct disk_header *dh = d->dh;
    unsigned int i, j, k, valid_sectors;
    off_t sz;

    sz = lseek(d->fd, 0, SEEK_END);
    if ( sz != 160*512*11 )
    {
        if ( !quiet )
            warnx("ADF file bad size: %lu bytes", (unsigned long)sz);
        memfree(d);
        return NULL;
    }
    lseek(d->fd, 0, SEEK_SET);

    adf_init(d);

    for ( i = 0; i < dh->nr_tracks; i++ )
    {
        th = &dh->track[i];
        read_exact(d->fd, d->track_data[i], th->len);
        valid_sectors = 0;
        for ( j = 0; j < 11; j++ )
        {
            unsigned char *p = d->track_data[i] + j*512;
            for ( k = 0; k < 512/4; k++ )
                if ( memcmp(p+k*4, "NDOS", 4) )
                    break;
            if ( k != 512/4 )
                valid_sectors |= 1u << j;
        }
        write_valid_sector_map(th, valid_sectors);
    }

    return d;
}

static void adf_close(struct disk *d)
{
    struct disk_header *dh = d->dh;
    unsigned int i;

    for ( i = 0; i < dh->nr_tracks; i++ )
        write_exact(d->fd, d->track_data[i], 11*512);
}

static void *adf_write_mfm(
    unsigned int tracknr, struct track_header *th, struct stream *s)
{
    unsigned int i;
    void *dat;

    stream_reset(s, tracknr);
    stream_next_index(s);
    dat = amigados_handler.write_mfm(tracknr, th, s);    

    if ( dat == NULL )
    {
        dat = adf_init_track(th);
    }
    else if ( th->type == TRKTYP_amigados_labelled )
    {
        th->type = TRKTYP_amigados;
        th->bytes_per_sector = 512;
        th->len = th->nr_sectors * th->bytes_per_sector;
        for ( i = 0; i < th->nr_sectors; i++ )
            memmove((char *)dat + i * 512,
                    (char *)dat + i * (512 + 16) + 16,
                    512);
    }

    return dat;
}


/**********************************
 * PRIVATE HELPERS
 */

uint32_t track_valid_sector_map(struct track_header *th)
{
    int i, j;
    uint32_t x = 0;

    for ( i = 2; i >= 0; i-- )
    {
        uint8_t b = th->valid_sector[i];
        for ( j = 0; j < 8; j++ )
        {
            x <<= 1;
            if ( b & (1u << j) )
                x |= 1;
        }
    }

    return x;
}

void write_valid_sector_map(struct track_header *th, uint32_t map)
{
    unsigned int i, j;

    for ( i = 0; i < 3; i++ )
    {
        uint8_t b = 0;
        for ( j = 0; j < 8; j++ )
        {
            if ( map & 1 )
                b |= (0x80u >> j);
            map >>= 1;
        }
        th->valid_sector[i] = b;
    }
}

void tbuf_init(struct track_buffer *tbuf)
{
    unsigned int bytes = tbuf->len + 7 / 8;
    tbuf->pos = tbuf->start;
    tbuf->mfm = memalloc(bytes);
    tbuf->speed = memalloc(2*bytes);
    tbuf->prev_data_bit = 0;
}

static void change_bit(uint8_t *map, unsigned int bit, bool_t on)
{
    if ( on )
        map[bit>>3] |= 0x80 >> (bit & 7);
    else
        map[bit>>3] &= ~(0x80 >> (bit & 7));
}

void tbuf_finalise(struct track_buffer *tbuf)
{
    int32_t end, pos;
    uint8_t b = 0;

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_all, 32, 0);

    pos = tbuf->start;
    for ( ; ; )
    {
        if ( --pos < 0 )
            pos += tbuf->len;
        if ( pos == tbuf->pos )
            break;
        change_bit(tbuf->mfm, pos, b);
        tbuf->speed[pos >> 3] = DEFAULT_SPEED;
        b = !b;
    }
}

void tbuf_bits(struct track_buffer *tbuf, uint16_t speed,
               enum tbuf_data_type type, unsigned int bits, uint32_t x)
{
    unsigned int i;

    if ( bits != 8 )
    {
        bits >>= 1;
        tbuf_bits(tbuf, speed, type, bits, x>>bits);
        tbuf_bits(tbuf, speed, type, bits, x);
        return;
    }

    if ( type == TBUFDAT_raw )
    {
        for ( i = 0; i < 8; i++ )
        {
            uint8_t b = !!((x << i) & 0x80);
            tbuf->prev_data_bit = b;
            change_bit(tbuf->mfm, tbuf->pos, b);
            tbuf->speed[tbuf->pos >> 3] = speed;
            if ( ++tbuf->pos >= tbuf->len )
                tbuf->pos = 0;
        }
    }
    else
    {
        unsigned int shift = (type == TBUFDAT_all);
        if ( type == TBUFDAT_even )
            x >>= 1;
        for ( i = 0; i < (8 << shift); i++ )
        {
            uint8_t b = !!((x << ((i|1) >> shift)) & 0x80); /* data bit */
            if ( !(i & 1) ) /* clock bit */
                b = (!tbuf->prev_data_bit && !b) << 7;
            else
                tbuf->prev_data_bit = b;
            change_bit(tbuf->mfm, tbuf->pos, b);
            tbuf->speed[tbuf->pos >> 3] = speed;
            if ( ++tbuf->pos >= tbuf->len )
                tbuf->pos = 0;
        }
    }
}

void tbuf_bytes(struct track_buffer *tbuf, uint16_t speed,
                enum tbuf_data_type type, unsigned int bytes, void *data)
{
    unsigned int i;
    for ( i = 0; i < bytes; i++ )
        tbuf_bits(tbuf, speed, type, 8, ((unsigned char *)data)[i]);
}
