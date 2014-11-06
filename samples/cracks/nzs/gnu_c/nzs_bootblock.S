#define load_off  0x0
#define load_size 0x30000
/*#define load_size 0x1600*/

start:
        /* Standard DOS bootblock header */
        dc.b    'D','O','S',0   /* signature */
        dc.l    0               /* checksum (filled later) */
        dc.l    880             /* dummy root block index */

        /* Save trackdisk ioreq for later use */
        move.l  a1,-(sp)

        /* Allocate chip memory for the data we're loading. Kickstarts earlier
         * than v36 require CMD_READ buffers to be in chip memory. */
        move.l  #load_size,d0   /* size */
        moveq   #3,d1           /* MEMF_PUBLIC | MEMF_CHIP */
        jsr     -0xc6(a6)       /* exec.AllocMem */
        move.l  d0,-(sp)
        jne     .load
.fail:  add.l   #8,sp
        moveq   #-1,d0
        rts

        /* Load data */
.load:  move.l  4(sp),a1
.retry: move.l  (sp),0x28(a1)   /* io_Data */
        move.l  #load_size,0x24(a1) /* io_Length */
        move.l  #load_off,0x2c(a1)  /* io_Offset */
        move.w  #2,0x1c(a1)     /* io_Command = CMD_READ */
        jsr     -0x1c8(a6)      /* exec.DoIO */
        move.l  4(sp),a1
        move.b  0x1f(a1),d0     /* check io_Error */
        jne     .retry          /* ...retry if failed */

        /* Allocate output buffer and stack */
        move.l  (sp),a0
        lea     (payload-start)(a0),a0
        move.l  4(a0),d0        /* size */
        add.l   #6000+512,d0    /* inflatetab + crctab */
        moveq   #3,d1           /* MEMF_PUBLIC | MEMF_CHIP */
        jsr     -0xc6(a6)       /* exec.AllocMem */
        move.l  d0,d1
        jeq     .fail
        move.l  (sp),a0
        lea     (payload-start)(a0),a1
        add.l   4(a1),d1
        add.l   #6000,d1
        move.l  d1,-(sp)        /* inflatebuf / crcbuf */
        move.l  d0,-(sp)        /* output */
        move.l  a0,-(sp)        /* input */
        
        move.l  d1,a1
        jbsr    crc16_ccitt_gentable
        move.l  (sp),a0
        lea     (payload-start)(a0),a0
        move.l  (a0),d1
        moveq   #-1,d0
        move.w  #0xf,0xdff180
        jbsr    crc16_ccitt
        jne     .retry
        move.l  (sp),a0
        lea     (1f-start)(a0),a0
        jmp     (a0)
1:      move.w  #0x7fff,0xdff096
        move.w  #0x7fff,0xdff09a
        move.l  4(sp),a4        /* a4 = output buffer */
        lea     payload(pc),a5  /* a5 = pack header */
        move.l  (a5),d1
        sub.l   #14,d1          /* d1 = deflate_bytes */
        moveq   #0,d0
        move.w  10(a5),d0
        add.l   4(a5),d0
        sub.l   d1,d0           /* d0 = out_bytes - deflate_bytes + leeway */
        lea     (a4,d0.l),a1    /* a1 = input copy dest */
        lea     12(a5),a0       /* a0 = input copy src */
        move.l  a1,a5
1:      move.b  (a0)+,(a1)+
        subq.l  #1,d1
        jne     1b
        move.w  #0xf00,0xdff180

        lea     (0xbfd000),a0
        moveq   #0,d2
        move.b  0xa00(a0),d2
        lsl.l   #8,d2
        move.b  0x900(a0),d2
        lsl.l   #8,d2
        move.b  0x800(a0),d2

        moveq   #0,d3
        move.b  0x1a01(a0),d3
        lsl.l   #8,d3
        move.b  0x1901(a0),d3
        lsl.l   #8,d3
        move.b  0x1801(a0),d3

        move.l  8(sp),a6
        jbsr    inflate_gentables
        jbsr    inflate_fromtables

        moveq   #0,d0
        move.b  0xa00(a0),d0
        lsl.l   #8,d0
        move.b  0x900(a0),d0
        lsl.l   #8,d0
        move.b  0x800(a0),d0

        moveq   #0,d1
        move.b  0x1a01(a0),d1
        lsl.l   #8,d1
        move.b  0x1901(a0),d1
        lsl.l   #8,d1
        move.b  0x1801(a0),d1

        sub.l   d2,d0
        sub.l   d3,d1
        move.l  d0,0x4
        move.l  d1,0x8
        
        move.w  #0xf,0xdff180
        move.l  a4,a0
        lea     payload(pc),a5
        move.l  4(a5),d1       /* d1 = hdr.out_bytes */
        moveq   #-1,d0          /* d0 = crc_in = 0xFFFF */
        move.l  8(sp),a1
        jbsr    crc16_ccitt
        cmp.w   8(a5),d0       /* check hdr.out_crc */
        bne     1f
        jmp     (a4)
1:      move.w  #0xff,0xdff180
        jra     1b

        /* We have loaded code from disk: Flush caches if possible */
        cmp.w   #37,0x14(a6)    /* exec.lib_version >= 37 */
        jmi     .done
        jsr     -0x27c(a6)      /* exec.CacheClearU */

        /* Clean up our stack and jump at what we just loaded */
.done:  movem.l (sp)+,a0-a1
        jmp     (a0)

#define OPT_TABLE_LOOKUP 1
#define OPT_INLINE_FUNCTIONS 1
#define OPT_UNROLL_COPY_LOOP 1
#define OPT_STORAGE_OFFSTACK 1
#define OPT_PREGENERATE_TABLES 1

#include "crc16_ccitt.S"
#include "inflate.S"

payload:
