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

offset_t *hex_search(FILE *fp, struct data hex, size_t *count) {
    size_t matched = 0;
    size_t offsize = STEP_SIZE; // has bugs when hex len very long
    offset_t *offsets = malloc(offsize * sizeof(offset_t));

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buffer = malloc(file_size);
    fread(buffer, 1, file_size, fp);
    for (int i = 0; i < file_size - hex.len + 1; i++) {
        if (memcmp(hex.buf, buffer+i, hex.len) == 0) {
            offsets[matched++] = i;
            if (matched == offsize) {
                offsize += STEP_SIZE;
                offsets = realloc(offsets, offsize * sizeof(offset_t));
            }
        }
    }
    *count = matched;
    free(buffer);
    return offsets;
}

int main(const int argc, char **argv) {
    int error = 0;

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

    FILE *fp = NULL;
    if (mode == SEARCH_MODE)
        fp = fopen(file_path, "rb");
    else
        fp = fopen(file_path, "rb+");
    if (fp == NULL) {
        error = 1;
        perror("fopen");
        goto exit;
    }

    size_t count;
    offset_t *offs = hex_search(fp, hex1, &count);

    printf("count: %zu\n", count);
    for (int i = 0; i < count; i++) {
        printf("%llu\n", offs[i]);
    }
    free(offs);

    free(hex1.buf);
    if (mode == PATCH_MODE) {
        free(hex2.buf);
    }

exit:
    return error;
}