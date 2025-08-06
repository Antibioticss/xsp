#ifndef PRIVATE_H
#define PRIVATE_H

#include <stdint.h>

#define CHUNK_SIZE     (64 * 1024)

struct data {
    size_t len;
    uint8_t *buf;
};

/* starts with 0, support negative index, ends with -1 */
struct range {
    int left, right;
};

extern bool print_help;
extern struct data hex1, hex2;
extern char *file_path;
extern struct range pat_range;

void usage();
int parse_arg(int argc, char **argv);

#endif