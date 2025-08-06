#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "private.h"
#include "skip/skip.h"

enum MODE {
    SEARCH_MODE,
    PATCH_MODE
} mode;

#define STEP_SIZE       256

#define max(a, b) ((a) > (b) ? (a) : (b))

static inline int update_range(struct range *rg, size_t total) {
    /* 
    convert negative range to positive one
    return 1 if succeed
    */
    if (rg->left < 0)
        rg->left += total;
    if (rg->right < 0)
        rg->right += total;
    if (0 <= rg->left && 
        rg->left <= rg->right && 
        rg->right < total)
        return 0;
    else {
        if (rg->left > rg->right)
            fprintf(stderr, "xsp: invalid range '%d,%d'\n", rg->left, rg->right);
        else
            fprintf(stderr, "xsp: range exceeded for total %zu matches\n", total);
        return 1;
    }
}

offset_t *hex_search(FILE *fp, struct data hex, size_t *count) {
    size_t matched = 0;
    size_t chunk_size = max(CHUNK_SIZE, hex.len * 2);
    skipidx_t skipidx;
    skip_init(&skipidx, hex.len, hex.buf);

    int cur_matched;
    offset_t *offsets = NULL;
    uint8_t *buffer = malloc(chunk_size + hex.len - 1);

    rewind(fp);
    size_t readc = fread(buffer + hex.len - 1, 1, chunk_size, fp);
    offsets = skip_match(&skipidx,
        buffer + hex.len - 1,         // this arg is different in the first match
        buffer + hex.len - 1 + readc,
        &cur_matched);
    matched += cur_matched;
    int offsize = (cur_matched & 0xffffff00) + STEP_SIZE; // & 0xffffff00 equals to (//256) * 256
    offsets = realloc(offsets, offsize * sizeof(offset_t));
    size_t filepos = chunk_size - (hex.len - 1); // bytes before the buffer
    while (readc == chunk_size) {
        memmove(buffer, buffer + chunk_size, hex.len - 1); // copy tail to head
        readc = fread(buffer + hex.len - 1, 1, chunk_size, fp);
        offset_t *cur_offs = skip_match(&skipidx,
            buffer,
            buffer + readc + hex.len - 1,
            &cur_matched);
        // merge the result
        if (matched + cur_matched > offsize) {
            offsize = max(matched + cur_matched, offsize + STEP_SIZE);
            offsets = realloc(offsets, offsize * sizeof(offset_t));
        }
        for (int i = 0; i < cur_matched; i++) {
            offsets[matched++] = filepos + cur_offs[i];
        }
        free(cur_offs);
        filepos += chunk_size;
    }
    *count = matched;
    free(buffer);
    skip_release(&skipidx);
    return offsets;
}

int hex_patch(FILE *fp, struct data hex, offset_t *offsets, struct range rg) {
    int patched = 0;
    for (int i = rg.left; i <= rg.right; i++) {
        if (fseek(fp, offsets[i], SEEK_SET) != 0) {
            perror("fseek");
            goto exit;
        }
        if (fwrite(hex.buf, hex.len, 1, fp) != 1) {
            perror("fwrite");
            goto exit;
        }
        patched++;
    }
exit:
    return patched;
}

int show_offsets(offset_t *offsets, struct range rg) {
    int shown = 0;
    for (int i = rg.left; i <= rg.right; i++) {
        printf("0x%llx\n", offsets[i]);
        shown++;
    }
    return shown;
}


int main(const int argc, char **argv) {
    int error = 0;
    FILE *fp = NULL;
    size_t count;
    offset_t *offs = NULL;

    if (parse_arg(argc, argv)) {
        return 1;
    }
    if (print_help) {
        usage();
        return 0;
    }

    // determine mode
    if (hex2.buf == NULL)
        mode = SEARCH_MODE;
    else
        mode = PATCH_MODE;

    if (mode == SEARCH_MODE)
        fp = fopen(file_path, "rb");
    else
        fp = fopen(file_path, "rb+");
    if (fp == NULL) {
        error = 1;
        perror("fopen");
        goto exit;
    }

    offs = hex_search(fp, hex1, &count);

    if (count == 0) {
        error = 1;
        printf("no matches found!");
        goto exit;
    }

    if (update_range(&pat_range, count) != 0) {
        error = 1;
        goto exit;
    }

    int expected = pat_range.right - pat_range.left + 1;
    int proceeded;
    if (mode == SEARCH_MODE) {
        proceeded = show_offsets(offs, pat_range);
        if (proceeded != expected)
            error = 1;
        printf("%d(%d) matches found\n", proceeded, expected);
    }
    else { // (mode == PATCH_MODE)
        proceeded = hex_patch(fp, hex2, offs, pat_range);
        if (proceeded != expected)
            error = 1;
        printf("%d(%d) matches patched\n", proceeded, expected);
    }

exit:
    free(offs);
    free(hex1.buf);
    if (mode == PATCH_MODE) {
        free(hex2.buf);
    }
    fclose(fp);
    return error;
}