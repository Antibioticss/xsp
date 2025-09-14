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
long base_offset = 0;

void usage() {
    puts("xsp - hex search & patch tool");
    puts("usage: xsp [options] hex1 [hex2]");
    puts("options:");
    puts("  -f <file>          path to the file to patch");
    puts("  -o, --offset <off> base offset to start search from (hex or decimal)");
    puts("  -r <range>         range of the matches, eg: '0,-1'");
    puts("  -t <threads>       number of threads to use (default: auto)");
    puts("  --str              treat args as string instead of hex string");
    puts("  --benchmark        run search performance benchmarks");
    puts("  -h, --help         print this usage");
    return;
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

struct data str2hex(const char *str, bool string_mode) {
    if (string_mode) {
        size_t len = strlen(str);
        uint8_t *hex = malloc(len);
        memcpy(hex, str, len);
        return (struct data){len, hex, NULL};
    }

    size_t capacity = strlen(str) / 2 + 1;
    uint8_t *bytes = malloc(capacity);
    uint8_t *wild = malloc(capacity);
    if (!bytes || !wild) {
        fprintf(stderr, "xsp: allocation failure\n");
        goto error;
    }
    size_t out_len = 0;

    size_t i = 0;
    while (str[i]) {
        while (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n') i++;
        if (!str[i]) break;

        if (str[i] == '?' && str[i+1] == '?') {
            if (out_len >= capacity) {
                capacity += 16;
                bytes = realloc(bytes, capacity);
                wild = realloc(wild, capacity);
                if (!bytes || !wild) {
                    fprintf(stderr, "xsp: allocation failure\n");
                    goto error;
                }
            }
            bytes[out_len] = 0x00;
            wild[out_len] = 1;
            out_len++;
            i += 2;
            continue;
        }

        int h1 = hex_value(str[i]);
        if (h1 < 0) {
            fprintf(stderr, "xsp: invalid character '%c' in hex string\n", str[i]);
            goto error;
        }
        i++;
        while (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n') i++;
        int h2 = hex_value(str[i]);
        if (h2 < 0) {
            fprintf(stderr, "xsp: invalid or incomplete hex pair near '%c'\n", str[i]);
            goto error;
        }
        if (out_len >= capacity) {
            capacity += 16;
            bytes = realloc(bytes, capacity);
            wild = realloc(wild, capacity);
            if (!bytes || !wild) {
                fprintf(stderr, "xsp: allocation failure\n");
                goto error;
            }
        }
        bytes[out_len] = (uint8_t)((h1 << 4) | h2);
        wild[out_len] = 0;
        out_len++;
        i++;
    }

    return (struct data){out_len, bytes, wild};

error:
    free(bytes);
    free(wild);
    return (struct data){0, NULL, NULL};
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
                if (strcmp("offset", cur + 2) == 0) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "xsp: --offset requires a value\n");
                        error = 1;
                        goto exit;
                    }
                    char *offset_str = argv[++i];
                    char *endptr;
                    // Support both hex (0x...) and decimal
                    if (strncmp(offset_str, "0x", 2) == 0 || strncmp(offset_str, "0X", 2) == 0) {
                        base_offset = strtol(offset_str, &endptr, 16);
                    } else {
                        base_offset = strtol(offset_str, &endptr, 10);
                    }
                    if (*endptr != '\0' || base_offset < 0) {
                        fprintf(stderr, "xsp: invalid offset '%s'\n", offset_str);
                        error = 1;
                        goto exit;
                    }
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
            if (cur[1] == 'o') {
                if (i + 1 >= argc) {
                    fprintf(stderr, "xsp: -o requires a value\n");
                    error = 1;
                    goto exit;
                }
                char *offset_str = argv[++i];
                char *endptr;
                // Support both hex (0x...) and decimal
                if (strncmp(offset_str, "0x", 2) == 0 || strncmp(offset_str, "0X", 2) == 0) {
                    base_offset = strtol(offset_str, &endptr, 16);
                } else {
                    base_offset = strtol(offset_str, &endptr, 10);
                }
                if (*endptr != '\0' || base_offset < 0) {
                    fprintf(stderr, "xsp: invalid offset '%s'\n", offset_str);
                    error = 1;
                    goto exit;
                }
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
            free(hex1.wildcard);
            error = 1;
            goto exit;
        }
        if (hex1.len != hex2.len) {
            free(hex1.buf);
            free(hex1.wildcard);
            free(hex2.buf);
            free(hex2.wildcard);
            fprintf(stderr, "xsp: hex string length mismatch!\n");
            error = 1;
            goto exit;
        }
        // if not string mode, validate replacement wildcards allowed only where find has wildcards
        if (!string_mode && hex2.wildcard && hex1.wildcard) {
            for (size_t i = 0; i < hex1.len; i++) {
                if (hex2.wildcard[i] && !hex1.wildcard[i]) {
                    fprintf(stderr, "xsp: invalid wildcard usage in replacement at byte %zu (?? not allowed where find is fixed)\n", i);
                    free(hex1.buf);
                    free(hex1.wildcard);
                    free(hex2.buf);
                    free(hex2.wildcard);
                    error = 1;
                    goto exit;
                }
            }
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
