#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "private.h"

bool print_help = false;
bool benchmark_mode = false;
struct data hex1, hex2;
char *file_path = NULL;
struct range pat_range = {0, -1};
int num_threads = 0;

void usage() {
    puts("xsp - hex search & patch tool");
    puts("usage: xsp [options] hex1 [hex2]");
    puts("options:");
    puts("  -f <file>          path to the file to patch");
    puts("  -r <range>         range of the matches, eg: '0,-1'");
    puts("  -t <threads>       number of threads to use (default: auto)");
    puts("  --str              treat args as string instead of hex string");
    puts("  --benchmark        run search performance benchmarks");
    puts("  -h, --help         print this usage");
    return;
}

struct data str2hex(const char *str, bool string_mode) {
    if (string_mode) {
        size_t len = strlen(str);
        uint8_t *hex = malloc(len);
        memcpy(hex, str, len);
        return (struct data){len, hex};
    }

    size_t len = 0;
    uint8_t *hex = malloc(strlen(str)/2 + 1);
    memset(hex, 0, strlen(str)/2 + 1);
    for (int i = 0; str[i]; i++) {
        uint8_t c = str[i];
        if (c >= '0' && c <= '9') hex[len>>1] |= (c-'0') << (((len+1)%2)*4), ++len;
        else if (c >= 'A' && c <= 'F') hex[len>>1] |= (c-'A'+10) << (((len+1)%2)*4), ++len;
        else if (c >= 'a' && c <= 'f') hex[len>>1] |= (c-'a'+10) << (((len+1)%2)*4), ++len;
        else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            fprintf(stderr, "xsp: invalid character '%c' in hex string\n", c);
            goto error;
        }
    }
    if (hex != NULL && len % 2 != 0) {
        fprintf(stderr, "xsp: hex string length should be oven\n");
        goto error;
    }
    len /= 2;
    return (struct data){len, hex};
    
error:
    free(hex);
    return (struct data){0, NULL};
}

int parse_arg(int argc, char **argv) {
    int error = 0;
    if (argc <= 1) {
        usage();
        return 1;
    }

    int argsc = 0;
    char **args = malloc(argc * sizeof(char*));
    char *range_str = NULL;
    bool string_mode = false;
    // start from 1, skip the first
    for (int i = 1; i < argc; i++) {
        char *cur = argv[i];
        if (cur[0] == '-') {
            if (cur[1] == '-') {
                if (strcmp("str", cur + 2) == 0) {
                    string_mode = true;
                    continue;
                }
                if (strcmp("help", cur + 2) == 0) {
                    print_help = true;
                    goto exit;
                }
                if (strcmp("benchmark", cur + 2) == 0) {
                    benchmark_mode = true;
                    continue;
                }
                fprintf(stderr, "xsp: unkown long argument '%s'\n", cur);
                error = 1;
                goto exit;
            }
            if (cur[2] != '\0') {
                fprintf(stderr, "xsp: unkown argument '%s'\n", cur);
                error = 1;
                goto exit;
            }
            if (cur[1] == 'f') {
                file_path = argv[++i];
                continue;
            }
            if (cur[1] == 'r') {
                range_str = argv[++i];
                continue;
            }
            if (cur[1] == 't') {
                if (i + 1 >= argc) {
                    fprintf(stderr, "xsp: -t requires a value\n");
                    error = 1;
                    goto exit;
                }
                num_threads = atoi(argv[++i]);
                if (num_threads < 0) {
                    fprintf(stderr, "xsp: invalid threads '%d'\n", num_threads);
                    error = 1;
                    goto exit;
                }
                continue;
            }
            if (cur[1] == 'h') {
                print_help = true;
                goto exit;
            }
            fprintf(stderr, "xsp: unkown argument '%s'\n", cur);
            error = 1;
            goto exit;
        }
        args[argsc++] = cur;
    }

    // benchmark mode doesn't require pattern arguments
    if (benchmark_mode) {
        if (argsc > 0) {
            fprintf(stderr, "xsp: benchmark mode doesn't accept pattern arguments\n");
            error = 1;
            goto exit;
        }
        goto exit; // skip pattern validation for benchmark mode
    }

    if (argsc < 1 || argsc > 2) {
        fprintf(stderr, "xsp: too less or too many arguments\n");
        error = 1;
        goto exit;
    }

    hex1 = str2hex(args[0], string_mode);
    if (hex1.buf == NULL) {
        error = 1;
        goto exit;
    }
    if (argsc == 2) {
        hex2 = str2hex(args[1], string_mode);
        if (hex2.buf == NULL) {
            free(hex1.buf);
            error = 1;
            goto exit;
        }
        if (hex1.len != hex2.len) {
            free(hex1.buf);
            free(hex2.buf);
            fprintf(stderr, "xsp: hex string length mismatch!\n");
            error = 1;
            goto exit;
        }
    }

    if (range_str != NULL) {
        if (sscanf(range_str, "%d,%d", &pat_range.left, &pat_range.right) != 2) {
            fprintf(stderr, "xsp: invalid range '%s'\n", range_str);
            error = 1;
            goto exit;
        }
        if ((pat_range.left >= 0 && pat_range.right >=0)
           || (pat_range.left < 0 && pat_range.right < 0)) {
            if (pat_range.left > pat_range.right) {
                fprintf(stderr, "xsp: invalid range '%s'\n", range_str);
                error = 1;
                goto exit;
            }
        }
    }

exit:
    free(args);
    return error;
}
