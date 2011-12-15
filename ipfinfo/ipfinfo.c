/*
 * ipfinfo.c
 * 
 * Read SPS IPF images.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>

/* read_exact, write_exact */
#include "../libdisk/util.c"

static int fd;

struct ipf_header {
    uint8_t id[4];
    uint32_t len;
    uint32_t crc;
};

struct ipf_info {
    uint32_t type;        /* 1 = FDD */
    uint32_t encoder;     /* 1 */
    uint32_t encrev;      /* 1 */
    uint32_t release;     /* 0x6666 (bogus, fake) */
    uint32_t revision;    /* 1 (bogus, fake)*/
    uint32_t origin;      /* 0 (bogus, fake) */
    uint32_t mincylinder; /* 0 */
    uint32_t maxcylinder; /* 83 */
    uint32_t minhead;     /* 0 */
    uint32_t maxhead;     /* 1 */
    uint32_t date;        /* year[2011-]*10000 + month[1-12]*100 + day[1-31] */
    uint32_t time;        /* h*10000000 + m*100000 + s*1000 + ms */
    uint32_t platform[4]; /* 1,0,0,0 */
    uint32_t disknum;     /* 0 */
    uint32_t userid;      /* 0 */
    uint32_t reserved[3]; /* 0,0,0 */
};

struct ipf_img {
    uint32_t cylinder; /* 0 - 83 */
    uint32_t head;     /* 0 or 1 */
    uint32_t dentype;  /* 1 = noise, 2 = auto, 3 = copylock */
    uint32_t sigtype;  /* 1 */
    uint32_t trksize;  /* ceil(trkbits/8) */
    uint32_t startpos; /* floor(startbit/8) */
    uint32_t startbit; /* bit offset from index of data start */
    uint32_t databits;
    uint32_t gapbits;
    uint32_t trkbits;  /* databits + gapbits */
    uint32_t blkcnt;   /* e.g., 11 for DOS */
    uint32_t process;  /* 0 */
    uint32_t flag;     /* 0 (unless flaky) */
    uint32_t dat_chunk;
    uint32_t reserved[3]; /* 0,0,0 */
};

struct ipf_data {
    uint32_t size;  /* ceil(bsize/8) */
    uint32_t bsize;
    uint32_t dcrc;  /* data area crc */
    uint32_t dat_chunk;
    /* Followed by #blocks ipf_block structures */
};

struct ipf_block {
    uint32_t blockbits;  /* decoded block size in bits */
    uint32_t gapbits;    /* decoded gap size in bits */
    union {
        struct {
            uint32_t blocksize;  /* ceil(blockbits/8) */
            uint32_t gapsize;    /* ceil(gapbits/8) */
        } caps;
        struct {
            uint32_t gapoffset;  /* 0 unless there is a gap stream */
            uint32_t celltype;   /* 1 for 2us MFM */
        } sps;
    } u;
    uint32_t enctype;    /* 1 */
    uint32_t flag;       /* 0 (bit 2 set means we count stream in bits!) */
    uint32_t gapvalue;   /* 0 */
    uint32_t dataoffset; /* offset of data stream in data area */
    /* Data is a set of chunks */
    /* Chunk start bytes is count_len[7:5], code[4:0] */
    /* count_len says how many following bytes contain (big endian) count */
    /* code is 0=end,1=sync,2=data,3=gap,4=raw,5=flakey */
};

static uint32_t encoder;

static void decode_info(const void *_info, unsigned int size)
{
    const struct ipf_info *info = _info;
    if (size != sizeof(*info))
        errx(1, "INFO size mismatch");
    encoder = info->encoder;
    if ((encoder < 1) || (encoder > 2))
        errx(1, "Unknown encoder type (%u)", encoder);
    printf("Type:      %u\n", info->type);
    printf("Encoder:   %u\n", info->encoder);
    printf("EncRev:    %u\n", info->encrev);
    printf("Release:   %u\n", info->release);
    printf("Revision:  %u\n", info->revision);
    printf("Origin:    %08x\n", info->origin);
    printf("MinCyl:    %u\n", info->mincylinder);
    printf("MaxCyl:    %u\n", info->maxcylinder);
    printf("MinHead:   %u\n", info->minhead);
    printf("MaxHead:   %u\n", info->maxhead);
    printf("Date:      %u/%u/%u\n", info->date/10000,
           (info->date/100)%100, (info->date)%100);
    printf("Time:      %u:%u:%u:%u\n",
           (info->time)/10000000,
           (info->time/100000)%100,
           (info->time/1000)%100,
           (info->time)%1000);
    printf("Platform:  %u/%u/%u/%u\n",
           info->platform[0],
           info->platform[1],
           info->platform[2],
           info->platform[3]);
    printf("DiskNum:   %u\n", info->disknum);
    printf("UserId:    %u\n", info->userid);
    printf("Rsvd:      %u/%u/%u\n",
           info->reserved[0],
           info->reserved[1],
           info->reserved[2]);
}

static void decode_img(const void *_img, unsigned int size)
{
    const struct ipf_img *img = _img;
    if (size != sizeof(*img))
        errx(1, "IMGE size mismatch");
    printf("Cylinder:  %u\n", img->cylinder);
    printf("Head:      %u\n", img->head);
    printf("DensiTyp:  %u\n", img->dentype);
    printf("SigTyp:    %u\n", img->sigtype);
    printf("TrackSize: %u\n", img->trksize);
    printf("StartPos:  %u\n", img->startpos);
    printf("StartBit:  %u\n", img->startbit);
    printf("DataBits:  %u\n", img->databits);
    printf("GapBits:   %u\n", img->gapbits);
    printf("TrkBits:   %u\n", img->trkbits);
    printf("BlkCnt:    %u\n", img->blkcnt);
    printf("Process:   %u\n", img->process);
    printf("Flag:      %u\n", img->flag);
    printf("DatChunk:  %u\n", img->dat_chunk);
    printf("Rsvd:      %u/%u/%u\n",
           img->reserved[0],
           img->reserved[1],
           img->reserved[2]);
}

static void decode_data(unsigned char *data, const char *name, uint32_t off)
{
    unsigned int i;
    if (!off)
        return;
    printf("%s: ", name);
    for (i = off; i < (off+16); i++)
        printf("%02x ", data[i]);
    printf("\n");
}

static void decode_block(void *_blk)
{
    struct ipf_block *blk = _blk;
    unsigned int i;
    for (i = 0; i < sizeof(struct ipf_block)/4; i++)
        ((uint32_t *)blk)[i] = be32toh(((uint32_t *)blk)[i]);
    printf("BlockBits: %u\n", blk->blockbits);
    printf("GapBits:   %u\n", blk->gapbits);
    if (encoder == 1) { /* CAPS */
        printf("BlockSize: %u\n", blk->u.caps.blocksize);
        printf("GapSize:   %u\n", blk->u.caps.gapsize);
    } else { /* SPS */
        printf("GapOffset: %u\n", blk->u.sps.gapoffset);
        printf("CellType:  %u\n", blk->u.sps.celltype);
    }
    printf("EncType:   %u\n", blk->enctype);
    printf("Flag:      %u\n", blk->flag);
    printf("GapValue:  %u\n", blk->gapvalue);
    printf("DataOffs:  %u\n", blk->dataoffset);
    if (encoder == 2)
        decode_data(_blk, "GAP", blk->u.sps.gapoffset);
    decode_data(_blk, "DAT", blk->dataoffset);
}

static void decode_dat(void *_dat, unsigned int size)
{
    uint32_t crc;
    unsigned char *data;
    struct ipf_data *dat = _dat;
    if (size != sizeof(*dat))
        errx(1, "DATA size mismatch");
    printf("Size:      %u\n", dat->size);
    printf("BSize:     %u\n", dat->bsize);
    printf("DCRC:      %08x\n", dat->dcrc);
    printf("DatChunk:  %u\n", dat->dat_chunk);
    data = malloc(dat->size);
    read_exact(fd, data, dat->size);
    crc = crc32(data, dat->size);
    if (dat->dcrc != crc)
        errx(1, "Data CRC mismatch");
    if (dat->size) {
        unsigned int i;
        for (i = 0; i < 11; i++) {
            printf("BLK %u\n", i);
            decode_block((char *)data + i*32);
        }
    }
    free(data);
}

int main(int argc, char **argv)
{
    struct ipf_header hdr;
    char *payload;
    char name[5] = { 0 };
    uint32_t crc, i, plen;

    if (argc != 2)
        errx(1, "Usage: ipfinfo <filename>");

    fd = open(argv[1], O_RDONLY);
    if (fd == -1)
        err(1, "%s", argv[1]);

    for (;;) {
        read_exact(fd, &hdr, sizeof(hdr));
        if (hdr.id[0] == '\0')
            break;
        crc = be32toh(hdr.crc);
        hdr.crc = 0;
        hdr.crc = crc32(&hdr, sizeof(hdr));
        hdr.len = be32toh(hdr.len);
        payload = malloc(hdr.len);
        plen = hdr.len - sizeof(hdr);
        read_exact(fd, payload, plen);
        hdr.crc = crc32_add(payload, plen, hdr.crc);
        strncpy(name, (char *)hdr.id, 4);
        printf("ID=%s len=%u crc=%08x\n", name, hdr.len, hdr.crc);
        if (hdr.crc != crc)
            errx(1, "CRC mismatch");
        for (i = 0; i < plen/4; i++)
            ((uint32_t *)payload)[i] = be32toh(((uint32_t *)payload)[i]);
        if (!strcmp(name, "INFO"))
            decode_info(payload, plen);
        if (!strcmp(name, "IMGE"))
            decode_img(payload, plen);
        if (!strcmp(name, "DATA"))
            decode_dat(payload, plen);
        free(payload);
    }

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
