/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK2 V2 (Pthreads SpMV - default OS scheduling)
*/

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define LINE_BUF 4096

typedef struct {
    int m, n;
    size_t nz;
    int *row;      // 0-based row index
    int *col;      // 0-based col index
    double *val;   // value
} COO;

typedef struct {
    int m, n;
    size_t nz;
    size_t *row_ptr; // length m+1
    int *col_idx;    // length nz
    double *a;       // length nz
} CSR;

typedef struct {
    const CSR *csr;
    size_t chunk_rows;

    // shared row pointer for dynamic scheduling
    int next_row;
    pthread_mutex_t lock;

    // per-thread checksums
    double *partial_sums;
} Shared;

typedef struct {
    Shared *S;
    int tid;
} ThreadArg;

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void ensure_results_dir() {
    if (mkdir("results", 0755) == -1) {
        if (errno != EEXIST) perror("mkdir results");
    }
}

/* x[j] = (j mod 1000)/1000.0, j is 1-based */
static double x_of_col(int j0) {
    int j = j0 + 1; // convert 0-based -> 1-based
    return (double)(j % 1000) / 1000.0;
}

/* Read Matrix Market (coordinate) into COO */
static int read_mtx_coo(const char *path, COO *out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char line[LINE_BUF];
    int m = 0, n = 0;
    long long nz_ll = 0;

    // Find first non-comment line containing: m n nz
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '%') continue;
        if (sscanf(line, "%d %d %lld", &m, &n, &nz_ll) == 3) break;
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
        if (line[0] == '%') continue;

        int r, c;
        double v;
        int cnt = sscanf(line, "%d %d %lf", &r, &c, &v);

        if (cnt < 2) continue;
        if (cnt == 2) v = 1.0; // missing value => assume 1.0

        r -= 1; // 1-based -> 0-based
        c -= 1;

        if (r < 0 || r >= m || c < 0 || c >= n) continue;

        row[idx] = r;
        col[idx] = c;
        val[idx] = v;
        idx++;
    }

    fclose(f);

    out->m = m;
    out->n = n;
    out->nz = idx; // in case file had fewer lines
    out->row = row;
    out->col = col;
    out->val = val;

    return (idx > 0);
}

/* Build CSR from first use_nz entries of COO (deterministic subset = first use_nz) */
static int build_csr_from_coo(const COO *coo, size_t use_nz, CSR *csr) {
    int m = coo->m;
    int n = coo->n;
    if (use_nz > coo->nz) use_nz = coo->nz;

    size_t *row_counts = (size_t*)calloc((size_t)m, sizeof(size_t));
    size_t *row_ptr = (size_t*)malloc(sizeof(size_t) * ((size_t)m + 1));
    int *col_idx = (int*)malloc(sizeof(int) * use_nz);
    double *a = (double*)malloc(sizeof(double) * use_nz);

    if (!row_counts || !row_ptr || !col_idx || !a) {
        fprintf(stderr, "Memory allocation failed for CSR.\n");
        free(row_counts); free(row_ptr); free(col_idx); free(a);
        return 0;
    }

    // count nnz per row
    for (size_t k = 0; k < use_nz; k++) {
        int r = coo->row[k];
        row_counts[(size_t)r]++;
    }

    // prefix sum
    row_ptr[0] = 0;
    for (int i = 0; i < m; i++) {
        row_ptr[i + 1] = row_ptr[i] + row_counts[(size_t)i];
    }

    size_t *next_pos = (size_t*)malloc(sizeof(size_t) * (size_t)m);
    if (!next_pos) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(row_counts); free(row_ptr); free(col_idx); free(a);
        return 0;
    }

    for (int i = 0; i < m; i++) next_pos[i] = row_ptr[i];

    // fill arrays
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

/* Worker: dynamic scheduling by row blocks */
static void* worker_v2(void *arg) {
    ThreadArg *A = (ThreadArg*)arg;
    Shared *S = A->S;
    const CSR *csr = S->csr;

    double local_sum = 0.0;

    while (1) {
        int start_row;

        pthread_mutex_lock(&S->lock);
        start_row = S->next_row;
        S->next_row += (int)S->chunk_rows;
        pthread_mutex_unlock(&S->lock);

        if (start_row >= csr->m) break;

        int end_row = start_row + (int)S->chunk_rows;
        if (end_row > csr->m) end_row = csr->m;

        for (int i = start_row; i < end_row; i++) {
            double sum = 0.0;
            size_t start = csr->row_ptr[i];
            size_t end = csr->row_ptr[i + 1];

            for (size_t k = start; k < end; k++) {
                int j0 = csr->col_idx[k];
                sum += csr->a[k] * x_of_col(j0);
            }
            local_sum += sum;
        }
    }

    S->partial_sums[A->tid] = local_sum;
    return NULL;
}

static double spmv_checksum_v2(const CSR *csr, int T, size_t chunk_rows) {
    Shared S;
    S.csr = csr;
    S.chunk_rows = chunk_rows;
    S.next_row = 0;
    pthread_mutex_init(&S.lock, NULL);

    S.partial_sums = (double*)calloc((size_t)T, sizeof(double));
    if (!S.partial_sums) {
        pthread_mutex_destroy(&S.lock);
        return 0.0;
    }

    pthread_t *th = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)T);
    ThreadArg *args = (ThreadArg*)malloc(sizeof(ThreadArg) * (size_t)T);

    for (int i = 0; i < T; i++) {
        args[i].S = &S;
        args[i].tid = i;
        pthread_create(&th[i], NULL, worker_v2, &args[i]);
    }

    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);

    double checksum = 0.0;
    for (int i = 0; i < T; i++) checksum += S.partial_sums[i];

    free(S.partial_sums);
    free(th);
    free(args);
    pthread_mutex_destroy(&S.lock);

    return checksum;
}

static void append_result_csv(const char *csvpath, int version, int threads,
                              size_t chunk, int trial, double ms) {
    FILE *f = fopen(csvpath, "r");
    int exists = (f != NULL);
    if (f) fclose(f);

    f = fopen(csvpath, "a");
    if (!f) return;

    if (!exists) fprintf(f, "task,version,threads,chunk,trial,time_ms\n");
    fprintf(f, "task2,%d,%d,%zu,%d,%.3f\n", version, threads, chunk, trial, ms);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <webbase-1M.mtx> <threads> <chunk_rows>\n", argv[0]);
        fprintf(stderr, "Example: %s webbase-1M.mtx 8 1024\n", argv[0]);
        return 1;
    }

    ensure_results_dir();

    const char *path = argv[1];
    int T = atoi(argv[2]);
    size_t chunk_rows = (size_t)atoll(argv[3]);
    if (T < 1) T = 1;
    if (chunk_rows < 1) chunk_rows = 1;

    COO coo;
    memset(&coo, 0, sizeof(coo));
    if (!read_mtx_coo(path, &coo)) return 1;

    size_t nz_total = coo.nz;
    size_t nz_sizes[3];
    nz_sizes[0] = (size_t)(nz_total * 0.10); if (nz_sizes[0] < 1) nz_sizes[0] = 1;
    nz_sizes[1] = (size_t)(nz_total * 0.50); if (nz_sizes[1] < 1) nz_sizes[1] = 1;
    nz_sizes[2] = nz_total;

    double full_checksum = 0.0;
    size_t full_nz_used = nz_total;

    for (int si = 0; si < 3; si++) {
        size_t use_nz = nz_sizes[si];

        CSR csr;
        memset(&csr, 0, sizeof(csr));

        // Build CSR outside timed region
        if (!build_csr_from_coo(&coo, use_nz, &csr)) {
            free(coo.row); free(coo.col); free(coo.val);
            return 1;
        }

        double last_cs = 0.0;

        // Timed region: SpMV + checksum only
        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            double cs = spmv_checksum_v2(&csr, T, chunk_rows);
            double t1 = now_ms();

            double ms = t1 - t0;
            append_result_csv("results/task2.csv", 2, T, chunk_rows, trial, ms);
            last_cs = cs;
        }

        if (si == 2) {
            full_checksum = last_cs;
            full_nz_used = use_nz;
        }

        free_csr(&csr);
    }

    // Required output (for full dataset)
    printf("M %d N %d NZ %zu\n", coo.m, coo.n, full_nz_used);
    printf("CHECKSUM %.6f\n", full_checksum);

    free(coo.row);
    free(coo.col);
    free(coo.val);

    return 0;
}
