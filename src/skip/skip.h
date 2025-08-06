#ifndef skip_h
#define skip_h

#include <stdint.h>

/* alphabet size 0x0-0xff */
#define ASIZE 0x100

typedef unsigned long long offset_t;
typedef struct {
    int val;
    int nxt;
} node_t;
typedef struct {
    size_t plen;
    unsigned char *patt;
    int *buck;
    node_t *buff;
} skipidx_t;

void skip_init(skipidx_t *idx, size_t patlen, const unsigned char *pattern);

/*
end won't be reached!
[start, end)
end = start + len
*/
offset_t *skip_match(skipidx_t *idx, unsigned char *start, unsigned char *end, int *count);

void skip_release(skipidx_t *idx);

#endif
