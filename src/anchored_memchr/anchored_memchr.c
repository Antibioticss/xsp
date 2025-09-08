#include <stdlib.h>
#include <string.h>

#include "anchored_memchr.h"

#define STEP_SIZE 256

void anchored_memchr_init(anchored_memchr_idx_t *idx, size_t patlen, const unsigned char *pattern)
{
    int bufidx = 1;
    unsigned char *patt = (unsigned char *)malloc((patlen + 1) * sizeof(unsigned char));
    int *bucket = (int *)malloc(ASIZE * sizeof(int));
    node_t *buffer = (node_t *)malloc((patlen + 1) * sizeof(node_t));
    memcpy(patt, pattern, patlen);
    memset(bucket, 0, ASIZE * sizeof(int));
    for (int i = 0; i < patlen; i++)
    {
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

offset_t *anchored_memchr_match(anchored_memchr_idx_t *idx, unsigned char *start, unsigned char *end, int *count)
{
    size_t patlen = idx->plen;
    unsigned char *pattern = idx->patt;
    int *bucket = idx->buck;
    node_t *buffer = idx->buff;

    int matched = 0;
    size_t off_size = STEP_SIZE;
    offset_t *offs = malloc(off_size * sizeof(offset_t));
    unsigned char *edge = end - patlen - 1;
    unsigned char *chbase = start + patlen - 1;
    for (; chbase <= edge; chbase += patlen)
    {
        for (int j = bucket[*chbase]; j; j = buffer[j].nxt)
        {
            unsigned char *cur = chbase - buffer[j].val;
            if (memcmp(pattern, cur, patlen) == 0)
            {
                offs[matched++] = (offset_t)(cur - start);
                if (matched == off_size)
                {
                    off_size += STEP_SIZE;
                    offs = realloc(offs, off_size * sizeof(offset_t));
                }
            }
        }
    }
    for (int i = bucket[*chbase]; i; i = buffer[i].nxt)
    {
        unsigned char *cur = chbase - buffer[i].val;
        if (cur + patlen <= end)
        {
            if (memcmp(pattern, cur, patlen) == 0)
            {
                offs[matched++] = (offset_t)(cur - start);
                if (matched == off_size)
                {
                    off_size += STEP_SIZE;
                    offs = realloc(offs, off_size * sizeof(offset_t));
                }
            }
        }
    }
    *count = matched;
    return offs;
}

void anchored_memchr_release(anchored_memchr_idx_t *idx)
{
    free(idx->patt);
    free(idx->buck);
    free(idx->buff);
    idx->patt = NULL;
    idx->buck = NULL;
    idx->buff = NULL;
    return;
}

int anchored_memchr_has_wildcards(const unsigned char *wildcard_mask, size_t patlen)
{
    if (wildcard_mask == NULL)
        return 0;
    for (size_t i = 0; i < patlen; i++)
    {
        if (wildcard_mask[i])
            return 1;
    }
    return 0;
}

offset_t *anchored_memchr_match_wildcard(unsigned char *start,
                                         unsigned char *end,
                                         const unsigned char *pattern,
                                         const unsigned char *wildcard_mask,
                                         size_t patlen,
                                         int *count)
{
    *count = 0;
    size_t n = (size_t)(end - start);
    if (patlen == 0 || n < patlen)
    {
        return (offset_t *)malloc(0);
    }

    int firstIndex = -1, lastIndex = -1;
    unsigned char firstByte = 0, lastByte = 0;
    for (size_t i = 0; i < patlen; i++)
    {
        if (!wildcard_mask || wildcard_mask[i] == 0)
        {
            firstIndex = (int)i;
            firstByte = pattern[i];
            break;
        }
    }
    for (size_t i = patlen; i-- > 0;)
    {
        if (!wildcard_mask || wildcard_mask[i] == 0)
        {
            lastIndex = (int)i;
            lastByte = pattern[i];
            break;
        }
    }
    if (firstIndex < 0 || lastIndex < 0)
    {
        return (offset_t *)malloc(0);
    }

    size_t offcap = STEP_SIZE;
    size_t matched = 0;
    offset_t *offs = (offset_t *)malloc(offcap * sizeof(offset_t));

    unsigned char *scanPtr = start + lastIndex;
    unsigned char *endPtr = start + (n - patlen + lastIndex + 1);
    while (scanPtr < endPtr)
    {
        size_t remaining = (size_t)(endPtr - scanPtr);
        void *foundRaw = memchr((const void *)scanPtr, (int)lastByte, remaining);
        if (foundRaw == NULL)
            break;
        unsigned char *found = (unsigned char *)foundRaw;
        size_t candidateShift = (size_t)(found - start) - (size_t)lastIndex;
        if (start[candidateShift + (size_t)firstIndex] == firstByte)
        {
            int ok = 1;
            for (size_t i = 0; i < patlen; i++)
            {
                if (i == (size_t)firstIndex || i == (size_t)lastIndex)
                    continue;
                if (wildcard_mask && wildcard_mask[i])
                    continue;
                if (start[candidateShift + i] != pattern[i])
                {
                    ok = 0;
                    break;
                }
            }
            if (ok)
            {
                if (matched == offcap)
                {
                    offcap += STEP_SIZE;
                    offs = (offset_t *)realloc(offs, offcap * sizeof(offset_t));
                }
                offs[matched++] = (offset_t)candidateShift;
            }
        }
        scanPtr = found + 1;
    }
    *count = (int)matched;
    return offs;
}