#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static struct node {
    uint32_t w, len, code;
    struct node *l, *r;
} leaf[256], internal[256];

static struct node *heap[256];
static int heap_nr;

static void heap_add(struct node *n)
{
    unsigned int i = ++heap_nr;
    while ((i != 1) && (heap[i>>1]->w > n->w)) {
        heap[i] = heap[i>>1];
        i >>= 1;
    }
    heap[i] = n;
}

static struct node *heap_pop(void)
{
    struct node *n, *smallest;
    unsigned int i = 1, j;
    if (!heap_nr)
        return NULL;
    smallest = heap[i];
    n = heap[heap_nr--];
    while ((j = 2*i) <= heap_nr) {
        if ((j+1 <= heap_nr) && (heap[j+1]->w < heap[j]->w))
            j++;
        if (heap[j]->w >= n->w)
            break;
        heap[i] = heap[j];
        i = j;
    }
    heap[i] = n;
    return smallest;
}

static void assign_codes(struct node *n, uint32_t code, uint32_t len)
{
    if (len > 32)
        errx(1, "Internal error: code too long");
    n->code = code;
    n->len = len;
    if (!n->l) return;
    assign_codes(n->l, (code<<1)|0, len+1);
    assign_codes(n->r, (code<<1)|1, len+1);
}

int main(int argc, char **argv)
{
    unsigned char *buf, *end, *p, *q, *nbuf;
    uint32_t scale, bits;
    int fd, origsz, sz, newsz, i, max_i, iters;
    uint32_t inmask, outmask;
    struct node *n;

    if (argc != 3)
        errx(1, "Usage: huffman <in> <out>");

    fd = open(argv[1], O_RDONLY);
    if (fd == -1)
        err(1, "%s", argv[1]);
    if ((sz = lseek(fd, 0, SEEK_END)) < 0)
        err(1, NULL);
    if ((buf = malloc(sz)) == NULL)
        err(1, NULL);
    lseek(fd, 0, SEEK_SET);
    if (read(fd, buf, sz) != sz)
        err(1, NULL);
    close(fd);
    origsz = sz;

    for (iters = 0;; iters++) {

        memset(leaf, 0, sizeof(leaf));
        memset(internal, 0, sizeof(internal));

        for (i = 0, p = buf; i < sz; i++, p++)
            leaf[*p].w++;

        for (i = max_i = 0; i < 256; i++)
            if (leaf[i].w > leaf[max_i].w)
                max_i = i;

        scale = ((uint32_t)~0) / leaf[max_i].w;
        for (i = 0; i < 256; i++) {
            if (!leaf[i].w)
                continue;
            leaf[i].w = (leaf[i].w*scale)>>24 ?: 1;
            heap_add(&leaf[i]);
        }

        for (n = internal; ; n++) {
            n->l = heap_pop();
            n->r = heap_pop();
            if (!n->r)
                break;
            n->w = n->l->w + n->r->w;
            heap_add(n);
        }
        assign_codes(n->l, 0, 0);

        bits = 0;
        for (i = 0, p = buf; i < sz; i++, p++)
            bits += leaf[*p].len;
        newsz = (bits+7)/8 + 256 + 2;

        if (newsz >= sz)
            break;

        nbuf = malloc(newsz);
        memset(nbuf, 0, newsz);
        *(uint16_t *)nbuf = htobe16(newsz);
        for (i = 0; i < 256; i++)
            nbuf[i+2] = leaf[i].w;
        q = &nbuf[256+2];
        outmask = 1u<<7;

        p = buf;
        end = buf + sz;
        while (p < end) {
            n = &leaf[*p++];
            for (inmask = (1u<<(n->len-1)); inmask; inmask >>= 1) {
                if (n->code & inmask)
                    *q |= outmask;
                if (!(outmask >>= 1)) {
                    outmask = 1u<<7;
                    q++;
                }
            }
        }
        if (outmask != (1u<<7))
            q++;

        if (q != (nbuf + newsz))
            errx(1, "Internal error: output length mismatch");

        free(buf);
        buf = nbuf;
        sz = newsz;
    }

    if (!iters)
        errx(1, "Unable to compress the input file");
    
    printf("After %u iterations: Old=%u, New=%u (%u%% saved)\n",
           iters, origsz, sz, ((origsz-sz)*100+(origsz/2))/origsz);

    fd = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd == -1)
        err(1, "%s", argv[2]);
    if (write(fd, buf, sz) != sz)
        err(1, NULL);
    close(fd);

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
