#include <stdlib.h>
#include <string.h>

#include "anchored_memchr.h"

#define STEP_SIZE       256

void anchored_memchr_init(anchored_memchr_idx_t *idx, size_t patlen, const unsigned char *pattern) {
    int bufidx = 1;
    unsigned char *patt = (unsigned char *)malloc((patlen + 1) * sizeof(unsigned char));
    int *bucket = (int *)malloc(ASIZE * sizeof(int));
    node_t *buffer = (node_t *)malloc((patlen + 1) * sizeof(node_t));
    memcpy(patt, pattern, patlen);
    memset(bucket, 0, ASIZE * sizeof(int));
    for (int i = 0; i < patlen; i++) {
        buffer[bufidx].val = i;
        buffer[bufidx].nxt = bucket[pattern[i]];
        bucket[pattern[i]] = bufidx;
        bufidx++;
    }
    idx->plen = patlen;
    idx->patt = patt;
    idx->buck = bucket;
    idx->buff = buffer;
    return;
}

offset_t *anchored_memchr_match(anchored_memchr_idx_t *idx, unsigned char *start, unsigned char *end, int *count) {
    size_t patlen = idx->plen;
    unsigned char *pattern = idx->patt;
    int *bucket = idx->buck;
    node_t *buffer = idx->buff;

    int matched = 0;
    size_t off_size = STEP_SIZE;
    offset_t *offs = malloc(off_size * sizeof(offset_t));
    unsigned char *edge = end - patlen - 1;
    unsigned char *chbase = start + patlen - 1;
    for (; chbase <= edge; chbase += patlen) {
        for (int j = bucket[*chbase]; j; j = buffer[j].nxt) {
            unsigned char *cur = chbase - buffer[j].val;
            if (memcmp(pattern, cur, patlen) == 0) {
                offs[matched++] = (offset_t)(cur - start);
                if (matched == off_size) {
                    off_size += STEP_SIZE;
                    offs = realloc(offs, off_size * sizeof(offset_t));
                }
            }
        }
    }
    for (int i = bucket[*chbase]; i; i = buffer[i].nxt) {
        unsigned char *cur = chbase - buffer[i].val;
        if (cur + patlen <= end) {
            if (memcmp(pattern, cur, patlen) == 0) {
                offs[matched++] = (offset_t)(cur - start);
                if (matched == off_size) {
                    off_size += STEP_SIZE;
                    offs = realloc(offs, off_size * sizeof(offset_t));
                }
            }
        }
    }
    *count = matched;
    return offs;
}

void anchored_memchr_release(anchored_memchr_idx_t *idx) {
    free(idx->patt);
    free(idx->buck);
    free(idx->buff);
    idx->patt = NULL;
    idx->buck = NULL;
    idx->buff = NULL;
    return;
}