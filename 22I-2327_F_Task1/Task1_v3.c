/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK1 V3 (Pthreads + affinity + static scheduling)
*/

#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>

#define FEATS 18
#define LINE_BUF 4096

static const double BIAS = -0.35;
static const double W[FEATS] = {
    0.12, -0.07, 0.05, 0.09, -0.11, 0.03, 0.08, -0.02,
    0.06, 0.04, -0.05, 0.10, -0.08, 0.07, 0.02, -0.03,
    0.11, -0.06
};

typedef struct {
    unsigned long long pred_pos, tp, fp, tn, fn;
} Counters;

typedef struct {
    const unsigned char *y;
    const float *x;
    size_t useN;
    size_t chunk;
    int tid;
    int T;
    int cpu_count;
    Counters local;
} ThreadArg;

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* skip commas, spaces, or tabs */
static void skip_separators(const char **p) {
    while (**p == ',' || **p == ' ' || **p == '\t' || **p == '\r') {
        (*p)++;
    }
}

/* parse y + 18 features, works for CSV or TSV */
static int parse_line(const char *line, unsigned char *y_out, float *x_out) {
    const char *p = line;
    char *endp = NULL;

    skip_separators(&p);
    double y = strtod(p, &endp);
    if (endp == p) return 0;
    *y_out = (unsigned char)((y >= 0.5) ? 1 : 0);

    p = endp;
    skip_separators(&p);

    for (int i = 0; i < FEATS; i++) {
        double v = strtod(p, &endp);
        if (endp == p) return 0;
        x_out[i] = (float)v;
        p = endp;
        skip_separators(&p);
    }
    return 1;
}

/* Load all records into arrays (skips first header line) */
static int load_csv(const char *path, unsigned char **labels, float **features, size_t *N) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char buf[LINE_BUF];
    size_t count = 0;

    // skip header line
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }

    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\n' || buf[0] == '\0') continue;
        count++;
    }
    fclose(f);

    if (count == 0) return 0;

    *labels = (unsigned char*)malloc(count);
    *features = (float*)malloc(count * FEATS * sizeof(float));
    if (!*labels || !*features) {
        free(*labels);
        free(*features);
        return 0;
    }

    f = fopen(path, "r");
    if (!f) { perror("fopen"); free(*labels); free(*features); return 0; }

    // skip header line again
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        free(*labels);
        free(*features);
        return 0;
    }

    size_t idx = 0;
    while (idx < count && fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\n' || buf[0] == '\0') continue;

        unsigned char y;
        float x[FEATS];

        if (!parse_line(buf, &y, x)) {
            fprintf(stderr, "Parse error at line %zu\n", idx + 2); // +2 because header skipped
            fclose(f);
            free(*labels);
            free(*features);
            return 0;
        }

        (*labels)[idx] = y;
        for (int i = 0; i < FEATS; i++) {
            (*features)[idx * FEATS + i] = x[i];
        }
        idx++;
    }

    fclose(f);
    *N = idx;
    return 1;
}

/* Pin current thread to a CPU core */
static void set_affinity(int tid, int cpu_count) {
    if (cpu_count < 1) return;

    int core = tid % cpu_count;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static void* worker_v3(void *arg) {
    ThreadArg *A = (ThreadArg*)arg;
    set_affinity(A->tid, A->cpu_count);

    Counters c = (Counters){0,0,0,0,0};
    size_t total_chunks = (A->useN + A->chunk - 1) / A->chunk;

    // static schedule: chunk_id = tid, tid+T, tid+2T, ...
    for (size_t chunk_id = (size_t)A->tid; chunk_id < total_chunks; chunk_id += (size_t)A->T) {
        size_t start = chunk_id * A->chunk;
        size_t end = start + A->chunk;
        if (end > A->useN) end = A->useN;

        for (size_t j = start; j < end; j++) {
            double s = BIAS;
            const float *row = &A->x[j * FEATS];

            for (int i = 0; i < FEATS; i++) {
                s += W[i] * (double)row[i];
            }

            double p = 1.0 / (1.0 + exp(-s));
            unsigned char pred = (p >= 0.5) ? 1 : 0;

            if (pred) c.pred_pos++;

            unsigned char yy = A->y[j];
            if (yy == 1 && pred == 1) c.tp++;
            else if (yy == 0 && pred == 1) c.fp++;
            else if (yy == 0 && pred == 0) c.tn++;
            else c.fn++;
        }
    }

    A->local = c;
    return NULL;
}

static Counters run_compute_v3(const unsigned char *y, const float *x,
                              size_t useN, int T, size_t chunk) {

    pthread_t *th = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)T);
    ThreadArg *args = (ThreadArg*)malloc(sizeof(ThreadArg) * (size_t)T);

    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1) cpu_count = 1;

    for (int i = 0; i < T; i++) {
        args[i].y = y;
        args[i].x = x;
        args[i].useN = useN;
        args[i].chunk = chunk;
        args[i].tid = i;
        args[i].T = T;
        args[i].cpu_count = cpu_count;
        args[i].local = (Counters){0,0,0,0,0};

        pthread_create(&th[i], NULL, worker_v3, &args[i]);
    }

    for (int i = 0; i < T; i++) {
        pthread_join(th[i], NULL);
    }

    Counters g = (Counters){0,0,0,0,0};
    for (int i = 0; i < T; i++) {
        g.pred_pos += args[i].local.pred_pos;
        g.tp += args[i].local.tp;
        g.fp += args[i].local.fp;
        g.tn += args[i].local.tn;
        g.fn += args[i].local.fn;
    }

    free(th);
    free(args);
    return g;
}

/* Append timing results to CSV (creates header if file doesn't exist) */
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
    fprintf(f, "task1,%d,%d,%zu,%d,%.3f\n", version, threads, chunk, trial, ms);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <susy.csv> <threads> <chunk>\n", argv[0]);
        fprintf(stderr, "Example: %s supersymmetry_dataset.csv 8 200000\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int T = atoi(argv[2]);
    size_t chunk = (size_t)atoll(argv[3]);
    if (T < 1) T = 1;
    if (chunk < 1) chunk = 1;

    // Make sure results folder exists (you can also do mkdir results manually)
    // If you want no extra headers, just create folder manually and remove this comment.
    // We'll just rely on user doing: mkdir -p results

    unsigned char *labels = NULL;
    float *features = NULL;
    size_t N = 0;

    if (!load_csv(path, &labels, &features, &N)) return 1;

    size_t sizes[3];
    sizes[0] = (size_t)(N * 0.10); if (sizes[0] < 1) sizes[0] = N;
    sizes[1] = (size_t)(N * 0.50); if (sizes[1] < 1) sizes[1] = N;
    sizes[2] = N;

    for (int si = 0; si < 3; si++) {
        size_t useN = sizes[si];
        Counters lastC = (Counters){0,0,0,0,0};

        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            Counters c = run_compute_v3(labels, features, useN, T, chunk);
            double t1 = now_ms();

            double ms = t1 - t0;
            append_result_csv("results/task1.csv", 3, T, chunk, trial, ms);

            lastC = c;
        }

        // Required exact 6 lines
        printf("N %zu\n", useN);
        printf("PRED_POS %llu\n", lastC.pred_pos);
        printf("TP %llu\n", lastC.tp);
        printf("FP %llu\n", lastC.fp);
        printf("TN %llu\n", lastC.tn);
        printf("FN %llu\n", lastC.fn);
    }

    free(labels);
    free(features);
    return 0;
}
