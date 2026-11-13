/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK2 V1 (Serial SpMV)
*/

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define LINE_BUF 4096

typedef struct {
    int m, n;
    size_t nz;
    int *row;      // 0-based row index (length nz)
    int *col;      // 0-based col index (length nz)
    double *val;   // value (length nz)
} COO;

typedef struct {
    int m, n;
    size_t nz;
    size_t *row_ptr; // length m+1
    int *col_idx;    // length nz
    double *a;       // length nz
} CSR;

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void ensure_results_dir() {
    if (mkdir("results", 0755) == -1) {
        if (errno != EEXIST) {
            perror("mkdir results");
        }
    }
}

/* Read Matrix Market coordinate format into COO */
static int read_mtx_coo(const char *path, COO *out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        return 0;
    }

    char line[LINE_BUF];

    // Skip header/comments: lines starting with '%'
    // Find first non-comment line that has: m n nz
    int m = 0, n = 0;
    long long nz_ll = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '%') continue;
        if (sscanf(line, "%d %d %lld", &m, &n, &nz_ll) == 3) {
            break;
        }
    }

    if (m <= 0 || n <= 0 || nz_ll <= 0) {
        fprintf(stderr, "Failed to read matrix size line (m n nz).\n");
        fclose(f);
        return 0;
    }

    size_t nz = (size_t)nz_ll;

    int *row = (int*)malloc(sizeof(int) * nz);
    int *col = (int*)malloc(sizeof(int) * nz);
    double *val = (double*)malloc(sizeof(double) * nz);

    if (!row || !col || !val) {
        fprintf(stderr, "Memory allocation failed for COO.\n");
        free(row); free(col); free(val);
        fclose(f);
        return 0;
    }

    size_t idx = 0;

    while (idx < nz && fgets(line, sizeof(line), f)) {
        if (line[0] == '%') continue; // sometimes comments appear later too

        int r, c;
        double v;
        int cnt = sscanf(line, "%d %d %lf", &r, &c, &v);

        if (cnt < 2) continue;        // skip bad/empty lines
        if (cnt == 2) v = 1.0;        // missing value => assume 1.0

        // Convert 1-based to 0-based
        r -= 1;
        c -= 1;

        if (r < 0 || r >= m || c < 0 || c >= n) {
            // If file has weird line, skip it (but usually it won't)
            continue;
        }

        row[idx] = r;
        col[idx] = c;
        val[idx] = v;
        idx++;
    }

    fclose(f);

    if (idx == 0) {
        fprintf(stderr, "No nonzeros read.\n");
        free(row); free(col); free(val);
        return 0;
    }

    // Sometimes files may have fewer lines than nz declared (rare). We’ll use what we got.
    out->m = m;
    out->n = n;
    out->nz = idx;
    out->row = row;
    out->col = col;
    out->val = val;

    return 1;
}

/* Build CSR from first use_nz entries of COO (deterministic subset = first use_nz) */
static int build_csr_from_coo(const COO *coo, size_t use_nz, CSR *csr) {
    int m = coo->m;
    int n = coo->n;

    if (use_nz > coo->nz) use_nz = coo->nz;

    size_t *row_ptr = (size_t*)calloc((size_t)m + 1, sizeof(size_t));
    int *col_idx = (int*)malloc(sizeof(int) * use_nz);
    double *a = (double*)malloc(sizeof(double) * use_nz);
    size_t *row_counts = (size_t*)calloc((size_t)m, sizeof(size_t));
    size_t *next_pos = NULL;

    if (!row_ptr || !col_idx || !a || !row_counts) {
        fprintf(stderr, "Memory allocation failed for CSR.\n");
        free(row_ptr); free(col_idx); free(a); free(row_counts);
        return 0;
    }

    // Count nnz per row for first use_nz entries
    for (size_t k = 0; k < use_nz; k++) {
        int r = coo->row[k];
        row_counts[(size_t)r]++;
    }

    // Prefix sum into row_ptr
    row_ptr[0] = 0;
    for (int i = 0; i < m; i++) {
        row_ptr[i + 1] = row_ptr[i] + row_counts[(size_t)i];
    }

    next_pos = (size_t*)malloc(sizeof(size_t) * (size_t)m);
    if (!next_pos) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(row_ptr); free(col_idx); free(a); free(row_counts);
        return 0;
    }

    for (int i = 0; i < m; i++) {
        next_pos[i] = row_ptr[i];
    }

    // Fill CSR arrays
    for (size_t k = 0; k < use_nz; k++) {
        int r = coo->row[k];
        size_t pos = next_pos[r]++;
        col_idx[pos] = coo->col[k];
        a[pos] = coo->val[k];
    }

    free(row_counts);
    free(next_pos);

    csr->m = m;
    csr->n = n;
    csr->nz = use_nz;
    csr->row_ptr = row_ptr;
    csr->col_idx = col_idx;
    csr->a = a;

    return 1;
}

static void free_csr(CSR *csr) {
    free(csr->row_ptr);
    free(csr->col_idx);
    free(csr->a);
    csr->row_ptr = NULL;
    csr->col_idx = NULL;
    csr->a = NULL;
}

static double x_of_col(int j0) {
    // j is 1-based in definition, j0 is 0-based
    int j = j0 + 1;
    return (double)(j % 1000) / 1000.0;
}

/* Compute checksum = sum_i y[i], where y = A*x (CSR) */
static double spmv_checksum_serial(const CSR *csr) {
    double checksum = 0.0;

    for (int i = 0; i < csr->m; i++) {
        double sum = 0.0;
        size_t start = csr->row_ptr[i];
        size_t end = csr->row_ptr[i + 1];

        for (size_t k = start; k < end; k++) {
            int j0 = csr->col_idx[k];
            double xj = x_of_col(j0);
            sum += csr->a[k] * xj;
        }
        checksum += sum;
    }

    return checksum;
}

static void append_result_csv(const char *csvpath, int version, int threads,
                              size_t chunk, int trial, double ms) {
    FILE *f = fopen(csvpath, "r");
    int exists = (f != NULL);
    if (f) fclose(f);

    f = fopen(csvpath, "a");
    if (!f) return;

    if (!exists) {
        fprintf(f, "task,version,threads,chunk,trial,time_ms\n");
    }
    fprintf(f, "task2,%d,%d,%zu,%d,%.3f\n", version, threads, chunk, trial, ms);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <webbase-1M.mtx>\n", argv[0]);
        return 1;
    }

    ensure_results_dir();

    COO coo;
    memset(&coo, 0, sizeof(coo));

    if (!read_mtx_coo(argv[1], &coo)) {
        return 1;
    }

    // Deterministic subsets: first 10%, first 50%, full (by nz lines)
    size_t nz_total = coo.nz;
    size_t nz_sizes[3];
    nz_sizes[0] = (size_t)(nz_total * 0.10);
    if (nz_sizes[0] < 1) nz_sizes[0] = 1;
    nz_sizes[1] = (size_t)(nz_total * 0.50);
    if (nz_sizes[1] < 1) nz_sizes[1] = 1;
    nz_sizes[2] = nz_total;

    // Chunk meaning for task2 = rows (or row-blocks) per chunk.
    // V1 doesn’t schedule work, but we still log a reasonable chunk number.
    size_t chunk_rows = 1024;

    double full_checksum = 0.0;
    size_t full_nz_used = nz_total;

    for (int si = 0; si < 3; si++) {
        size_t use_nz = nz_sizes[si];

        CSR csr;
        memset(&csr, 0, sizeof(csr));

        // Build CSR OUTSIDE timed region (no file I/O inside timing)
        if (!build_csr_from_coo(&coo, use_nz, &csr)) {
            free(coo.row); free(coo.col); free(coo.val);
            return 1;
        }

        double best = 1e18, sum = 0.0;
        double last_checksum = 0.0;

        // Timed region: SpMV + checksum ONLY
        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            double cs = spmv_checksum_serial(&csr);
            double t1 = now_ms();

            double ms = t1 - t0;
            sum += ms;
            if (ms < best) best = ms;

            append_result_csv("results/task2.csv", 1, 1, chunk_rows, trial, ms);
            last_checksum = cs;
        }

        // Save full checksum for required output at end
        if (si == 2) {
            full_checksum = last_checksum;
            full_nz_used = use_nz;
        }

        free_csr(&csr);
    }

    // REQUIRED output signature for FULL dataset only (2 lines)
    printf("M %d N %d NZ %zu\n", coo.m, coo.n, full_nz_used);
    printf("CHECKSUM %.6f\n", full_checksum);

    free(coo.row);
    free(coo.col);
    free(coo.val);

    return 0;
}
