#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "private.h"
#include "anchored_memchr/anchored_memchr.h"

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

typedef struct {
    unsigned char *base_ptr;
    size_t base_offset;        // absolute offset in file
    size_t chunk_size;         // assigned non-overlapped chunk length
    size_t file_size;          // total file size
    struct data pattern;       // pattern to search
    bool is_last;              // is last segment
    offset_t *results;         // absolute offsets found (allocated)
    int result_count;          // number of results
} search_task_t;

static void *search_worker(void *arg) {
    search_task_t *task = (search_task_t *)arg;
    task->results = NULL;
    task->result_count = 0;

    const size_t pattern_length = task->pattern.len;
    if (pattern_length == 0) return NULL;

    // determine effective scan length including overlap but not beyond file end
    size_t max_span = task->chunk_size + (pattern_length > 0 ? (pattern_length - 1) : 0);
    size_t available = task->file_size - task->base_offset;
    size_t effective_len = max_span < available ? max_span : available;

    int local_count = 0;
    offset_t *local = NULL;
    if (!anchored_memchr_has_wildcards(task->pattern.wildcard, task->pattern.len)) {
        anchored_memchr_idx_t skipidx;
        anchored_memchr_init(&skipidx, task->pattern.len, task->pattern.buf);
        local = anchored_memchr_match(&skipidx,
                                 task->base_ptr,
                                 task->base_ptr + effective_len,
                                 &local_count);
        anchored_memchr_release(&skipidx);
    } else {
        local = anchored_memchr_match_wildcard(
            task->base_ptr,
            task->base_ptr + effective_len,
            task->pattern.buf,
            task->pattern.wildcard,
            task->pattern.len,
            &local_count);
    }

    // filter to avoid duplicates across segment boundaries and convert to absolute
    size_t cutoff = task->base_offset + task->chunk_size;
    bool apply_cutoff = !task->is_last; // last segment keeps all
    int kept = 0;
    for (int i = 0; i < local_count; i++) {
        offset_t abs_off = (offset_t)task->base_offset + local[i];
        if (!apply_cutoff || abs_off < (offset_t)cutoff) {
            local[kept++] = abs_off;
        }
    }
    task->results = local;
    task->result_count = kept;
    return NULL;
}

static int get_online_cpu_count() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) return 1;
    if (n > 256) n = 256; // sane upper bound
    return (int)n;
}

offset_t *hex_search(FILE *fp, struct data hex, size_t *count) {
    *count = 0;
    if (hex.len == 0) {
        return (offset_t *)malloc(0);
    }

    // determine file size
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long file_size_long = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    if (file_size_long <= 0) {
        return (offset_t *)malloc(0);
    }
    size_t total_file_size = (size_t)file_size_long;
    
    // apply base offset
    if (base_offset >= file_size_long) {
        return (offset_t *)malloc(0);
    }
    size_t search_start = (size_t)base_offset;
    size_t file_size = total_file_size - search_start;
    if (file_size < hex.len) {
        return (offset_t *)malloc(0);
    }

    int fd = fileno(fp);
    unsigned char *map = mmap(NULL, total_file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        // fallback: single-threaded buffered scan
        size_t matched = 0;
        size_t chunk_size = max(CHUNK_SIZE, hex.len * 2);
        int cur_matched;
        offset_t *offsets = NULL;
        uint8_t *buffer = malloc(chunk_size + hex.len - 1);

        fseek(fp, search_start, SEEK_SET);
        size_t readc = fread(buffer + hex.len - 1, 1, chunk_size, fp);
        if (!anchored_memchr_has_wildcards(hex.wildcard, hex.len)) {
            anchored_memchr_idx_t skipidx;
            anchored_memchr_init(&skipidx, hex.len, hex.buf);
            offsets = anchored_memchr_match(&skipidx,
                buffer + hex.len - 1,
                buffer + hex.len - 1 + readc,
                &cur_matched);
            anchored_memchr_release(&skipidx);
        } else {
            offsets = anchored_memchr_match_wildcard(
                buffer + hex.len - 1,
                buffer + hex.len - 1 + readc,
                hex.buf,
                hex.wildcard,
                hex.len,
                &cur_matched);
        }
        // adjust offsets to be absolute file positions
        for (int i = 0; i < cur_matched; i++) {
            offsets[i] += search_start;
        }
        matched += cur_matched;
        int offsize = (cur_matched & 0xffffff00) + STEP_SIZE;
        offsets = realloc(offsets, offsize * sizeof(offset_t));
        size_t filepos = search_start + chunk_size - (hex.len - 1);
        while (readc == chunk_size) {
            memmove(buffer, buffer + chunk_size, hex.len - 1);
            readc = fread(buffer + hex.len - 1, 1, chunk_size, fp);
            offset_t *cur_offs;
            if (!anchored_memchr_has_wildcards(hex.wildcard, hex.len)) {
                anchored_memchr_idx_t skipidx2;
                anchored_memchr_init(&skipidx2, hex.len, hex.buf);
                cur_offs = anchored_memchr_match(&skipidx2,
                    buffer,
                    buffer + readc + hex.len - 1,
                    &cur_matched);
                anchored_memchr_release(&skipidx2);
            } else {
                cur_offs = anchored_memchr_match_wildcard(
                    buffer,
                    buffer + readc + hex.len - 1,
                    hex.buf,
                    hex.wildcard,
                    hex.len,
                    &cur_matched);
            }
            if (matched + cur_matched > (size_t)offsize) {
                offsize = max(matched + cur_matched, (size_t)offsize + STEP_SIZE);
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
        return offsets;
    }

    int threads = num_threads;
    if (threads <= 0) threads = get_online_cpu_count();
    if (threads < 1) threads = 1;
    if ((size_t)threads > file_size) threads = (int)file_size; // avoid zero chunk

    size_t base_chunk = file_size / (size_t)threads;
    if (base_chunk == 0) base_chunk = 1;

    pthread_t *tids = (pthread_t *)malloc((size_t)threads * sizeof(pthread_t));
    search_task_t *tasks = (search_task_t *)malloc((size_t)threads * sizeof(search_task_t));

    for (int i = 0; i < threads; i++) {
        size_t thread_offset = (size_t)i * base_chunk;
        size_t remaining = file_size - thread_offset;
        size_t chunk_len = (i == threads - 1) ? remaining : base_chunk;
        tasks[i] = (search_task_t){
            .base_ptr = map + search_start + thread_offset,
            .base_offset = search_start + thread_offset,
            .chunk_size = chunk_len,
            .file_size = total_file_size,
            .pattern = hex,
            .is_last = (i == threads - 1),
            .results = NULL,
            .result_count = 0,
        };
        pthread_create(&tids[i], NULL, search_worker, &tasks[i]);
    }

    // merge results
    size_t matched_total = 0;
    size_t offcap = STEP_SIZE;
    offset_t *all_offs = (offset_t *)malloc(offcap * sizeof(offset_t));
    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
        int cnt = tasks[i].result_count;
        if (matched_total + (size_t)cnt > offcap) {
            size_t needed = matched_total + (size_t)cnt;
            while (offcap < needed) offcap += STEP_SIZE;
            all_offs = (offset_t *)realloc(all_offs, offcap * sizeof(offset_t));
        }
        if (cnt > 0 && tasks[i].results != NULL) {
            memcpy(all_offs + matched_total, tasks[i].results, (size_t)cnt * sizeof(offset_t));
            matched_total += (size_t)cnt;
            free(tasks[i].results);
        }
    }

    free(tids);
    free(tasks);
    munmap(map, total_file_size);

    *count = matched_total;
    return all_offs;
}

int hex_patch(FILE *fp, struct data hex, offset_t *offsets, struct range rg) {
    int patched = 0;
    for (int i = rg.left; i <= rg.right; i++) {
        // build write buffer honoring replacement wildcards (preserve original byte)
        uint8_t *writebuf = (uint8_t *)malloc(hex.len);
        if (!writebuf) goto exit;
        for (size_t j = 0; j < hex.len; j++) {
            if (hex.wildcard && hex.wildcard[j]) {
                if (fseek(fp, offsets[i] + (long)j, SEEK_SET) != 0) {
                    perror("fseek");
                    free(writebuf);
                    goto exit;
                }
                int c = fgetc(fp);
                if (c == EOF && ferror(fp)) {
                    perror("fread");
                    free(writebuf);
                    goto exit;
                }
                writebuf[j] = (uint8_t)c;
            } else {
                writebuf[j] = hex.buf[j];
            }
        }
        if (fseek(fp, offsets[i], SEEK_SET) != 0) {
            perror("fseek");
            free(writebuf);
            goto exit;
        }
        if (fwrite(writebuf, hex.len, 1, fp) != 1) {
            perror("fwrite");
            free(writebuf);
            goto exit;
        }
        free(writebuf);
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

double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// read a pattern of given length from a random offset in the file
static uint8_t *read_random_pattern_from_file(FILE *fp, size_t pattern_len, size_t file_size) {
    if (file_size < pattern_len) return NULL;
    long max_offset = (long)(file_size - pattern_len);
    long random_offset = 0;
    if (max_offset > 0) {
        random_offset = (long)(rand() % (max_offset + 1));
    }

    if (fseek(fp, random_offset, SEEK_SET) != 0) {
        return NULL;
    }
    uint8_t *buffer = malloc(pattern_len);
    if (!buffer) return NULL;
    size_t readc = fread(buffer, 1, pattern_len, fp);
    if (readc != pattern_len) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

// helper for qsort on doubles
static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

// compute median of an array (expects a copy; will sort it)
static double compute_median(double *values, size_t count) {
    qsort(values, count, sizeof(double), compare_doubles);
    if (count % 2 == 0) {
        return (values[count / 2 - 1] + values[count / 2]) / 2.0;
    } else {
        return values[count / 2];
    }
}

// filter outliers using median absolute deviation (MAD)
static size_t filter_outliers_mad(const double *input, size_t count, double *output) {
    if (count < 3) {
        // no filtering, copy as is
        for (size_t i = 0; i < count; i++) output[i] = input[i];
        return count;
    }

    // copy and sort to compute median
    double *sorted = malloc(count * sizeof(double));
    if (!sorted) {
        for (size_t i = 0; i < count; i++) output[i] = input[i];
        return count;
    }
    memcpy(sorted, input, count * sizeof(double));
    double median = compute_median(sorted, count);

    // compute absolute deviations
    double *deviations = malloc(count * sizeof(double));
    if (!deviations) {
        memcpy(output, input, count * sizeof(double));
        free(sorted);
        return count;
    }
    for (size_t i = 0; i < count; i++) {
        deviations[i] = fabs(input[i] - median);
    }
    double mad = compute_median(deviations, count);

    // if MAD is zero, avoid division by zero / over-filtering; return original set
    if (mad == 0.0) {
        memcpy(output, input, count * sizeof(double));
        free(sorted);
        free(deviations);
        return count;
    }

    double threshold = 2.5 * mad;
    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        if (fabs(input[i] - median) <= threshold) {
            output[kept++] = input[i];
        }
    }

    if (kept < count / 2) {
        // use lenient threshold
        double lenient = 3.5 * mad;
        kept = 0;
        for (size_t i = 0; i < count; i++) {
            if (fabs(input[i] - median) <= lenient) {
                output[kept++] = input[i];
            }
        }
    }

    free(sorted);
    free(deviations);
    return kept;
}

void run_benchmark(FILE *fp) {
    // seed randomness once
    srand((unsigned int)time(NULL));

    // determine file size once
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long file_size_long = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    if (file_size_long <= 0) {
        fprintf(stderr, "xsp: unable to determine file size for benchmark\n");
        return;
    }
    size_t file_size = (size_t)file_size_long;

    const size_t pattern_sizes[] = {8, 16, 32, 64, 128, 256};
    const size_t num_sizes = sizeof(pattern_sizes) / sizeof(pattern_sizes[0]);
    const int num_iterations = 20;

    for (size_t i = 0; i < num_sizes; i++) {
        size_t pattern_size = pattern_sizes[i];
        if (pattern_size > file_size) {
            printf("%-14zu skipped (pattern larger than file)\n", pattern_size);
            continue;
        }

        // collect durations in milliseconds
        double *durations = malloc(num_iterations * sizeof(double));
        if (!durations) {
            fprintf(stderr, "xsp: allocation failed for durations\n");
            return;
        }
        int recorded = 0;

        for (int iter = 0; iter < num_iterations; iter++) {
            // sample a pattern from a random file offset (non-wildcard)
            uint8_t *pattern = read_random_pattern_from_file(fp, pattern_size, file_size);
            if (!pattern) {
                // if read failed, try next iteration
                continue;
            }

            struct data hex = {pattern_size, pattern};
            size_t count = 0;

            // measure search time
            double start_time = get_time_ms();
            offset_t *offsets = hex_search(fp, hex, &count);
            double end_time = get_time_ms();
            double elapsed_ms = end_time - start_time;

            // record and cleanup
            durations[recorded++] = elapsed_ms;
            free(offsets);
            free(pattern);
        }

        if (recorded == 0) {
            printf("%-14zu no samples\n", pattern_size);
            free(durations);
            continue;
        }

        // filter outliers using MAD
        double *filtered = malloc(recorded * sizeof(double));
        if (!filtered) {
            fprintf(stderr, "xsp: allocation failed for filtered durations\n");
            free(durations);
            return;
        }
        size_t kept = filter_outliers_mad(durations, (size_t)recorded, filtered);

        double sum = 0.0;
        for (size_t k = 0; k < kept; k++) sum += filtered[k];
        double avg_time = kept > 0 ? (sum / (double)kept) : 0.0;
        printf("%-14zu %.6fms\n", pattern_size, avg_time);

        free(durations);
        free(filtered);
    }
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

    // handle benchmark mode
    if (benchmark_mode) {
        if (file_path == NULL) {
            fprintf(stderr, "xsp: benchmark mode requires a file (-f <file>)\n");
            return 1;
        }
        fp = fopen(file_path, "rb");
        if (fp == NULL) {
            perror("fopen");
            return 1;
        }
        run_benchmark(fp);
        fclose(fp);
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
        printf("no matches found!\n");
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
    free(hex1.wildcard);
    if (mode == PATCH_MODE) {
        free(hex2.buf);
        free(hex2.wildcard);
    }
    fclose(fp);
    return error;
}
