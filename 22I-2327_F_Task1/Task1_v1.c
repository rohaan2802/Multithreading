/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK1 V1 (Serial)
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void skip_separators(const char **p) {
    while (**p == ',' || **p == ' ' || **p == '\t' || **p == '\r') {
        (*p)++;
    }
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
    if (!f) {
        perror("fopen");
        return 0;
    }

    // 1) count lines
    char buf[LINE_BUF];
    size_t count = 0;

    // skip header line
    if (!fgets(buf, sizeof(buf), f)) {
        fprintf(stderr, "Empty file or read error.\n");
        fclose(f);
        return 0;
    }




    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\n' || buf[0] == '\0') continue;
        count++;
    }
    fclose(f);

    if (count == 0) {
        fprintf(stderr, "No records found.\n");
        return 0;
    }


    *labels = (unsigned char*)malloc(count * sizeof(unsigned char));
    *features = (float*)malloc(count * FEATS * sizeof(float));
    if (!*labels || !*features) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(*labels);
        free(*features);
        return 0;
    }

    // 3) read again + parse
    f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        free(*labels);
        free(*features);
        return 0;
    }

    size_t idx = 0;

    // skip header line
    if (!fgets(buf, sizeof(buf), f)) {
        fprintf(stderr, "Unexpected read error.\n");
        fclose(f);
        free(*labels);
        free(*features);
        return 0;
    }

    
    while (idx < count && fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\n' || buf[0] == '\0') continue;
        unsigned char y;
        float x[FEATS];
        if (!parse_line(buf, &y, x)) {
            fprintf(stderr, "Parse error at line %zu\n", idx + 1);
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

static void compute_serial(const unsigned char *y, const float *x, size_t useN, Counters *out) {
    Counters c = {0,0,0,0,0};

    for (size_t j = 0; j < useN; j++) {
        double s = BIAS;
        const float *row = &x[j * FEATS];
        for (int i = 0; i < FEATS; i++) {
            s += W[i] * (double)row[i];
        }
        double p = 1.0 / (1.0 + exp(-s));
        unsigned char pred = (p >= 0.5) ? 1 : 0;

        if (pred) c.pred_pos++;

        if (y[j] == 1 && pred == 1) c.tp++;
        else if (y[j] == 0 && pred == 1) c.fp++;
        else if (y[j] == 0 && pred == 0) c.tn++;
        else if (y[j] == 1 && pred == 0) c.fn++;
    }

    *out = c;
}

static void append_result_csv(const char *csvpath, int version, int threads, size_t chunk, int trial, double ms) {
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <susy.csv>\n", argv[0]);
        return 1;
    }

    unsigned char *labels = NULL;
    float *features = NULL;
    size_t N = 0;

    if (!load_csv(argv[1], &labels, &features, &N)) {
        return 1;
    }

    // dataset sizes (10%, 50%, 100%)
    size_t sizes[3];
    sizes[0] = (size_t)(N * 0.10);
    if (sizes[0] < 1) sizes[0] = N;
    sizes[1] = (size_t)(N * 0.50);
    if (sizes[1] < 1) sizes[1] = N;
    sizes[2] = N;

    // chunk concept exists in assignment, but V1 has no scheduling; we still log something.
    size_t chunk = 200000;

    // Run 5 trials for each size
    for (int si = 0; si < 3; si++) {
        size_t useN = sizes[si];

        double best = 1e18, sum = 0.0;
        Counters finalC = {0,0,0,0,0};

        for (int t = 1; t <= 5; t++) {
            Counters c;
            double t0 = now_ms();
            compute_serial(labels, features, useN, &c);
            double t1 = now_ms();

            double ms = t1 - t0;
            sum += ms;
            if (ms < best) best = ms;
            finalC = c;

            append_result_csv("results/task1.csv", 1, 1, chunk, t, ms);
        }

       
        printf("N %zu\n", useN);
        printf("PRED_POS %llu\n", finalC.pred_pos);
        printf("TP %llu\n", finalC.tp);
        printf("FP %llu\n", finalC.fp);
        printf("TN %llu\n", finalC.tn);
        printf("FN %llu\n", finalC.fn);

        
    }

    free(labels);
    free(features);
    return 0;
}
