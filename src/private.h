#ifndef PRIVATE_H
#define PRIVATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#define CHUNK_SIZE     (64 * 1024)

struct data {
    size_t len;
    uint8_t *buf;
    /* when non-NULL, wildcard[i] == 1 means that byte is a wildcard (matches any) */
    uint8_t *wildcard;
};

/* starts with 0, support negative index, ends with -1 */
struct range {
    int left, right;
};

extern bool print_help;
extern bool benchmark_mode;
extern struct data hex1, hex2;
extern char *file_path;
extern struct range pat_range;
extern int num_threads;
extern long base_offset;

void usage();
int parse_arg(int argc, char **argv);
void run_benchmark(FILE *fp);

#endif
