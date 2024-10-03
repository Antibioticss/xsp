#include "hexpatch/hexpatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>

bool has_range = false;
int string_mode = 0, hex_len = 0;
char *hex1 = NULL, *hex2 = NULL, *filepath = NULL;
range pat_range = {0, -1};

void usage() {
    puts("xsp - hex search & patch tool");
    puts("usage: xsp [options] hex1 [hex2]");
    puts("options:");
    puts("  -f, --file <file>         path to the file to patch");
    puts("  -r, --range <range>       range of the matches, eg: '0,-1'");
    puts("  --str                     treat args as string instead of hex string");
    puts("  -h, --help                print this usage");
    return;
}

unsigned char *str_to_hex(const char *str, int *out_len) {
    int len = 0;
    unsigned char *hex = malloc(strlen(str) + 1);
    *out_len = 0;
    for (int i = 0; str[i]; i++) {
        unsigned char c = str[i];
        if (c >= '0' && c <= '9') hex[len>>1] |= (c-'0') << (((len+1)%2)*4), ++len;
        else if (c >= 'A' && c <= 'F') hex[len>>1] |= (c-'A'+10) << (((len+1)%2)*4), ++len;
        else if (c >= 'a' && c <= 'f') hex[len>>1] |= (c-'a'+10) << (((len+1)%2)*4), ++len;
        else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            fprintf(stderr, "xsp: invalid character '%c' in hex string\n", c);
            free(hex);
            return NULL;
        }
    }
    if (len%2 != 0) {
        fprintf(stderr, "xsp: hex string length should be oven\n");
        free(hex);
        return NULL;
    }
    *out_len = len / 2;
    return hex;
}

int parse_arguments(const int argc, char **argv) {
    if (argc <= 1) {
        usage();
        return 1;
    }
    while(1) {
        static struct option long_options[] = {
            {"file",    required_argument, 0,           'f'},
            {"range",   required_argument, 0,           'r'},
            {"help",    no_argument,       0,           'h'},
            {"str",     no_argument,       &string_mode, 1},
            {0, 0, 0, 0}
        };
        int option_index = 0;
        int c = getopt_long(argc, argv, "f:r:h", long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
        case 'f':
            filepath = optarg;
            break;
        case 'r':
            if (sscanf(optarg, "%d,%d", &pat_range.left, &pat_range.right) != 2) {
                fprintf(stderr, "xsp: invalid range '%s'\n", optarg);
                return 1;
            }
            has_range = true;
            break;
        case 'h':
            usage();
            return 0;
        case '?':
            return 1;
        default:
            break;
        }
    }

    const int left_count = argc - optind;
    if (left_count < 1) {
        fprintf(stderr, "xsp: arguments not enough!\n");
        return 1;
    }
    else if (left_count > 2) {
        fprintf(stderr, "xsp: too many arguments!\n");
        return 1;
    }

    if (filepath == NULL) {
        fprintf(stderr, "xsp: '-f' argument is required!\n");
        return 1;
    }

    hex1 = argv[optind++];
    if (string_mode) {
        hex_len = strlen(hex1);
        char *tmp = malloc(hex_len + 1);
        strcpy(tmp, hex1);
        hex1 = tmp;
    }
    else {
        unsigned char *tmp = str_to_hex(hex1, &hex_len);
        if (tmp != NULL)
            hex1 = (char*)tmp;
        else
            return 1;
    }
    if (left_count == 2) {
        hex2 = argv[optind++];
        if (string_mode) {
            if (hex_len == strlen(hex2)) {
                char *tmp = malloc(hex_len + 1);
                strcpy(tmp, hex2);
                hex2 = tmp;
            }
            else {
                fprintf(stderr, "xsp: hex string length mismatch!\n");
                free(hex1);
                return 1;
            }
        }
        else {
            int hex2_len;
            unsigned char *tmp = str_to_hex(hex2, &hex2_len);
            if (tmp != NULL && hex_len == hex2_len) {
                hex2 = (char*)tmp;
            }
            else {
                if (tmp != NULL) {
                    fprintf(stderr, "xsp: hex string length mismatch!\n");
                    free(tmp);
                }
                free(hex1);
                return 1;
            }
        }
    }
    return 0;
}

int main(const int argc, char **argv) {
    int error_code = 0;
    if (parse_arguments(argc, argv)) {
        return 1;
    }
    if (hex2 == NULL) {
        if (has_range) {
            error_code = 1;
            fprintf(stderr, "xsp: range only works for patching mode!\n");
        }
        else {
            int matched;
            FILE *fp = fopen(filepath, "rb");
            if (fp == NULL) {
                error_code = 1;
                perror("xsp");
            }
            else {
                long long *offsets = search_single(fp, 0, &matched, hex_len, hex1);
                if (matched == 0) {
                    error_code = 1;
                    printf("no matches found!\n");
                }
                else {
                    for (int i = 0; i < matched; i++)
                        printf("0x%llx\n", offsets[i]);
                    printf("found %d matches\n", matched);
                    fclose(fp);
                }
            }
        }
    }
    else {
        FILE *fp = fopen(filepath, "rb+");
        if (fp == NULL) {
            error_code = 1;
            perror("xsp");
        }
        else {
            PAT_RESULT pr = patch_single(fp, pat_range, hex_len, hex1, hex2);
            if (pr != PAT_SUCCESS) {
                error_code = 1;
                if (pr == PAT_NOTFOUND) {
                    printf("no matches found!\n");
                }
            }
            fclose(fp);
        }
    }
    return error_code;
}