/*
 * util.c
 * 
 * Little helper utils.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>

#include <ctype.h>
#include <unistd.h>

void __bug(const char *file, int line)
{
    warnx("BUG at %s:%d", file, line);
    abort();
}

void __warn(const char *file, int line)
{
    warnx("WARNING at %s:%d", file, line);
}

void filename_extension(const char *filename, char *extension, size_t size)
{
    const char *p;
    unsigned int i;

    extension[0] = '\0';
    if ((p = strrchr(filename, '.')) == NULL)
        return;

    for (i = 0; i < (size-1); i++)
        if ((extension[i] = tolower(p[i+1])) == '\0')
            break;
    extension[i] = '\0';
}

void *memalloc(size_t size)
{
    void *p = malloc(size?:1);
    if (p == NULL)
        err(1, NULL);
    memset(p, 0, size);
    return p;
}

void memfree(void *p)
{
    free(p);
}

void read_exact(int fd, void *buf, size_t count)
{
    ssize_t done;
    char *_buf = buf;

    while (count > 0) {
        done = read(fd, _buf, count);
        if (done < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            err(1, NULL);
        }
        if (done == 0) {
            memset(_buf, 0, count);
            done = count;
        }
        count -= done;
        _buf += done;
    }
}

void write_exact(int fd, const void *buf, size_t count)
{
    ssize_t done;
    const char *_buf = buf;

    while (count > 0) {
        done = write(fd, _buf, count);
        if (done < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            err(1, NULL);
        }
        count -= done;
        _buf += done;
    }
}

static uint32_t crc32_tab[256];
static void __initcall crc32_tab_init(void)
{
    unsigned int i, j;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (j = 0; j < 8; j++)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320 : 0);
        crc32_tab[i] = c;
    }
}

uint32_t crc32_add(const void *buf, size_t len, uint32_t crc)
{
    unsigned int i;
    const char *b = buf;
    crc = ~crc;
    for (i = 0; i < len; i++)
        crc = crc32_tab[(uint8_t)(crc ^ *b++)] ^ (crc >> 8);
    return ~crc;
}

uint32_t crc32(const void *buf, size_t len)
{
    return crc32_add(buf, len, 0);
}

uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc)
{
    unsigned int i;
    const uint8_t *b = buf;
    for (i = 0; i < len; i++) {
        crc  = (uint8_t)(crc >> 8) | (crc << 8);
        crc ^= *b++;
        crc ^= (uint8_t)crc >> 4;
        crc ^= crc << 12;
        crc ^= (crc & 0xff) << 5;
    }
    return crc;
}

uint16_t crc16_ccitt_bit(uint8_t b, uint16_t crc)
{
    if (!!b ^ (crc >> 15))
        crc = (crc << 1) ^ 0x1021;
    else
        crc <<= 1;
    return crc;
}

uint16_t rnd16(uint32_t *p_seed)
{
    *p_seed = *p_seed * 1103515245 + 12345;
    return *p_seed >> 16;
}

#if !defined(__PLATFORM_HAS_ENDIAN_H__)

uint16_t htobe16(uint16_t host_16bits)
{
    uint16_t result;
    ((uint8_t *)&result)[0] = host_16bits >> 8;
	((uint8_t *)&result)[1] = host_16bits;
    return result;
}

uint16_t htole16(uint16_t host_16bits)
{
    uint16_t result;
    ((uint8_t *)&result)[1] = host_16bits >> 8;
	((uint8_t *)&result)[0] = host_16bits;
    return result;
}

uint32_t htobe32(uint32_t host_32bits)
{
    uint32_t result;
    ((uint16_t *)&result)[0] = htobe16(host_32bits >> 16);
	((uint16_t *)&result)[1] = htobe16(host_32bits);
    return result;
}

uint32_t htole32(uint32_t host_32bits)
{
    uint32_t result;
    ((uint16_t *)&result)[1] = htole16(host_32bits >> 16);
	((uint16_t *)&result)[0] = htole16(host_32bits);
    return result;
}

uint64_t htobe64(uint64_t host_64bits)
{
    uint64_t result;
    ((uint32_t *)&result)[0] = htobe32(host_64bits >> 32);
	((uint32_t *)&result)[1] = htobe32(host_64bits);
    return result;
}

uint64_t htole64(uint64_t host_64bits)
{
    uint64_t result;
    ((uint32_t *)&result)[1] = htole32(host_64bits >> 32);
	((uint32_t *)&result)[0] = htole32(host_64bits);
    return result;
}

uint16_t be16toh(uint16_t big_endian_16bits)
{
    return ((((uint8_t *)&big_endian_16bits)[0] << 8) |
            ((uint8_t *)&big_endian_16bits)[1]);
}

uint16_t le16toh(uint16_t little_endian_16bits)
{
    return ((((uint8_t *)&little_endian_16bits)[1] << 8) |
            ((uint8_t *)&little_endian_16bits)[0]);
}

uint32_t be32toh(uint32_t big_endian_32bits)
{
    return ((be16toh(((uint16_t *)&big_endian_32bits)[0]) << 16) |
            be16toh(((uint16_t *)&big_endian_32bits)[1]));
}

uint32_t le32toh(uint32_t little_endian_32bits)
{
    return ((le16toh(((uint16_t *)&little_endian_32bits)[1]) << 16) |
            le16toh(((uint16_t *)&little_endian_32bits)[0]));
}

uint64_t be64toh(uint64_t big_endian_64bits)
{
    return (((uint64_t)be32toh(((uint32_t *)&big_endian_64bits)[0]) << 32) |
            be32toh(((uint32_t *)&big_endian_64bits)[1]));
}

uint64_t le64toh(uint64_t little_endian_64bits)
{
    return (((uint64_t)le32toh(((uint32_t *)&little_endian_64bits)[1]) << 32) |
            le32toh(((uint32_t *)&little_endian_64bits)[0]));
}

#endif /* !defined(__PLATFORM_HAS_ENDIAN_H__) */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
