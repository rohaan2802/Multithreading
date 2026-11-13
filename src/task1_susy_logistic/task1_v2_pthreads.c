/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK1 V2 (Pthreads default scheduling)
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

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

    // work queue index (shared)
    size_t next_idx;
    pthread_mutex_t lock;

    // global counters (shared)
    Counters global;
    pthread_mutex_t g_lock;
} Shared;

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int parse_line(const char *line, unsigned char *y_out, float *x_out) {
    const char *p = line;
    char *endp = NULL;

    double y = strtod(p, &endp);
    if (endp == p) return 0;
    *y_out = (unsigned char)((y >= 0.5) ? 1 : 0);

    p = endp;
    if (*p == ',') p++;

    for (int i = 0; i < FEATS; i++) {
        double v = strtod(p, &endp);
        if (endp == p) return 0;
        x_out[i] = (float)v;
        p = endp;
        if (*p == ',') p++;
    }
    return 1;
}

static int load_csv(const char *path, unsigned char **labels, float **features, size_t *N) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char buf[LINE_BUF];
    size_t count = 0;

    // skip header line
    if (!fgets(buf, sizeof(buf), f)) {
        fprintf(stderr, "Unexpected read error.\n");
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
        free(*labels); free(*features);
        return 0;
    }

    f = fopen(path, "r");
    if (!f) { perror("fopen"); free(*labels); free(*features); return 0; }

    size_t idx = 0;

        // skip header line
    if (!fgets(buf, sizeof(buf), f)) {
        fprintf(stderr, "Unexpected read error.\n");
        fclose(f);
        return 0;
    }

    
    while (idx < count && fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\n' || buf[0] == '\0') continue;
        unsigned char y;
        float x[FEATS];
        if (!parse_line(buf, &y, x)) {
            fprintf(stderr, "Parse error at line %zu\n", idx + 1);
            fclose(f);
            free(*labels); free(*features);
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

static void add_to_global(Shared *S, const Counters *c) {
    pthread_mutex_lock(&S->g_lock);
    S->global.pred_pos += c->pred_pos;
    S->global.tp += c->tp;
    S->global.fp += c->fp;
    S->global.tn += c->tn;
    S->global.fn += c->fn;
    pthread_mutex_unlock(&S->g_lock);
}

static void* worker_v2(void *arg) {
    Shared *S = (Shared*)arg;
    Counters local = {0,0,0,0,0};

    while (1) {
        size_t start;

        // take next chunk
        pthread_mutex_lock(&S->lock);
        start = S->next_idx;
        if (start >= S->useN) {
            pthread_mutex_unlock(&S->lock);
            break;
        }
        S->next_idx += S->chunk;
        pthread_mutex_unlock(&S->lock);

        size_t end = start + S->chunk;
        if (end > S->useN) end = S->useN;

        for (size_t j = start; j < end; j++) {
            double s = BIAS;
            const float *row = &S->x[j * FEATS];
            for (int i = 0; i < FEATS; i++) s += W[i] * (double)row[i];
            double p = 1.0 / (1.0 + exp(-s));
            unsigned char pred = (p >= 0.5) ? 1 : 0;

            if (pred) local.pred_pos++;

            unsigned char y = S->y[j];
            if (y == 1 && pred == 1) local.tp++;
            else if (y == 0 && pred == 1) local.fp++;
            else if (y == 0 && pred == 0) local.tn++;
            else local.fn++;
        }
    }

    add_to_global(S, &local);
    return NULL;
}

static void append_result_csv(const char *csvpath, int version, int threads, size_t chunk, int trial, double ms) {
    FILE *f = fopen(csvpath, "r");
    int exists = (f != NULL);
    if (f) fclose(f);

    f = fopen(csvpath, "a");
    if (!f) return;

    if (!exists) fprintf(f, "task,version,threads,chunk,trial,time_ms\n");
    fprintf(f, "task1,%d,%d,%zu,%d,%.3f\n", version, threads, chunk, trial, ms);
    fclose(f);
}

static Counters run_compute_v2(const unsigned char *y, const float *x, size_t useN, int T, size_t chunk) {
    Shared S;
    S.y = y; S.x = x; S.useN = useN; S.chunk = chunk;
    S.next_idx = 0;
    S.global = (Counters){0,0,0,0,0};
    pthread_mutex_init(&S.lock, NULL);
    pthread_mutex_init(&S.g_lock, NULL);

    pthread_t *th = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)T);
    for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, worker_v2, &S);
    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);

    pthread_mutex_destroy(&S.lock);
    pthread_mutex_destroy(&S.g_lock);
    free(th);
    return S.global;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <susy.csv> <threads> <chunk>\n", argv[0]);
        fprintf(stderr, "Example: %s SUSY.csv 8 200000\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int T = atoi(argv[2]);
    size_t chunk = (size_t)atoll(argv[3]);
    if (T < 1) T = 1;
    if (chunk < 1) chunk = 1;

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
        Counters lastC = {0,0,0,0,0};

        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            Counters c = run_compute_v2(labels, features, useN, T, chunk);
            double t1 = now_ms();
            double ms = t1 - t0;

            append_result_csv("results/task1.csv", 2, T, chunk, trial, ms);
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
