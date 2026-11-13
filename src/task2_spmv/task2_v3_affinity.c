/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK2 V3 (Pthreads SpMV + Affinity + Static Scheduling)
*/

#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>

#define LINE_BUF 4096

typedef struct {
    int m, n;
    size_t nz;
    int *row;      // 0-based row
    int *col;      // 0-based col
    double *val;   // value
} COO;

typedef struct {
    int m, n;
    size_t nz;
    size_t *row_ptr; // m+1
    int *col_idx;    // nz
    double *a;       // nz
} CSR;

typedef struct {
    const CSR *csr;
    int tid;
    int T;
    int cpu_count;
    size_t chunk_rows;
    double partial_sum;
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

/* deterministic x[j] = (j mod 1000)/1000.0, j is 1-based */
static double x_of_col(int j0) {
    int j = j0 + 1;
    return (double)(j % 1000) / 1000.0;
}

/* pin current thread to a CPU core */
static void set_affinity(int tid, int cpu_count) {
    if (cpu_count < 1) return;

    int core = tid % cpu_count;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

/* Read Matrix Market coordinate to COO */
static int read_mtx_coo(const char *path, COO *out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char line[LINE_BUF];
    int m = 0, n = 0;
    long long nz_ll = 0;

    // find size line (skip comments)
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
        if (cnt == 2) v = 1.0;

        r -= 1;
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
    out->nz = idx;
    out->row = row;
    out->col = col;
    out->val = val;

    return (idx > 0);
}

/* Build CSR from first use_nz entries (deterministic subset) */
static int build_csr_from_coo(const COO *coo, size_t use_nz, CSR *csr) {
    if (use_nz > coo->nz) use_nz = coo->nz;

    int m = coo->m;
    int n = coo->n;

    size_t *row_counts = (size_t*)calloc((size_t)m, sizeof(size_t));
    size_t *row_ptr = (size_t*)malloc(sizeof(size_t) * ((size_t)m + 1));
    int *col_idx = (int*)malloc(sizeof(int) * use_nz);
    double *a = (double*)malloc(sizeof(double) * use_nz);

    if (!row_counts || !row_ptr || !col_idx || !a) {
        fprintf(stderr, "Memory allocation failed for CSR.\n");
        free(row_counts); free(row_ptr); free(col_idx); free(a);
        return 0;
    }

    for (size_t k = 0; k < use_nz; k++) {
        int r = coo->row[k];
        row_counts[(size_t)r]++;
    }

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

/*
V3 worker: static scheduling with chunk rows
Thread tid processes blocks:
start_row = tid*chunk, (tid+T)*chunk, (tid+2T)*chunk, ...
*/
static void* worker_v3(void *arg) {
    ThreadArg *A = (ThreadArg*)arg;
    const CSR *csr = A->csr;

    set_affinity(A->tid, A->cpu_count);

    double local_sum = 0.0;

    int m = csr->m;
    int T = A->T;
    size_t chunk = A->chunk_rows;

    size_t total_blocks = ((size_t)m + chunk - 1) / chunk;

    for (size_t block_id = (size_t)A->tid; block_id < total_blocks; block_id += (size_t)T) {
        int start_row = (int)(block_id * chunk);
        int end_row = start_row + (int)chunk;
        if (end_row > m) end_row = m;

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

    A->partial_sum = local_sum;
    return NULL;
}

static double spmv_checksum_v3(const CSR *csr, int T, size_t chunk_rows) {
    pthread_t *th = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)T);
    ThreadArg *args = (ThreadArg*)malloc(sizeof(ThreadArg) * (size_t)T);

    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1) cpu_count = 1;

    for (int i = 0; i < T; i++) {
        args[i].csr = csr;
        args[i].tid = i;
        args[i].T = T;
        args[i].cpu_count = cpu_count;
        args[i].chunk_rows = chunk_rows;
        args[i].partial_sum = 0.0;
        pthread_create(&th[i], NULL, worker_v3, &args[i]);
    }

    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);

    double checksum = 0.0;
    for (int i = 0; i < T; i++) checksum += args[i].partial_sum;

    free(th);
    free(args);
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

        // build CSR outside timed region
        if (!build_csr_from_coo(&coo, use_nz, &csr)) {
            free(coo.row); free(coo.col); free(coo.val);
            return 1;
        }

        double last_cs = 0.0;

        // timed region: spmv + checksum only
        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            double cs = spmv_checksum_v3(&csr, T, chunk_rows);
            double t1 = now_ms();

            double ms = t1 - t0;
            append_result_csv("results/task2.csv", 3, T, chunk_rows, trial, ms);
            last_cs = cs;
        }

        if (si == 2) {
            full_checksum = last_cs;
            full_nz_used = use_nz;
        }

        free_csr(&csr);
    }

    // required output signature for full dataset
    printf("M %d N %d NZ %zu\n", coo.m, coo.n, full_nz_used);
    printf("CHECKSUM %.6f\n", full_checksum);

    free(coo.row);
    free(coo.col);
    free(coo.val);

    return 0;
}
