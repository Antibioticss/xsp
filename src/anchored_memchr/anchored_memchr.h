#ifndef anchored_memchr_h
#define anchored_memchr_h

#include <stdint.h>
#include <stddef.h>

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
} anchored_memchr_idx_t;

void anchored_memchr_init(anchored_memchr_idx_t *idx, size_t patlen, const unsigned char *pattern);

/*
end won't be reached!
[start, end)
end = start + len
*/
offset_t *anchored_memchr_match(anchored_memchr_idx_t *idx, unsigned char *start, unsigned char *end, int *count);

void anchored_memchr_release(anchored_memchr_idx_t *idx);

int anchored_memchr_has_wildcards(const unsigned char *wildcard_mask, size_t patlen);

offset_t *anchored_memchr_match_wildcard(unsigned char *start,
                                         unsigned char *end,
                                         const unsigned char *pattern,
                                         const unsigned char *wildcard_mask,
                                         size_t patlen,
                                         int *count);

#endif


