/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK3 V3 (Parallel BFS + Affinity + Static Scheduling)
*/


#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>

#define LINE_BUF 4096
#define SOURCE_NODE 0

typedef struct {
    int *u;
    int *v;
    size_t E;
    int maxNode;
} EdgeList;

typedef struct {
    int n;
    size_t E;
    size_t *out_start; // n+1
    int *out_adj;      // E
} GraphCSR;

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

static int read_edges(const char *path, EdgeList *out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char line[LINE_BUF];
    size_t E = 0;
    int maxNode = -1;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        int a, b;
        if (sscanf(line, "%d %d", &a, &b) == 2) {
            E++;
            if (a > maxNode) maxNode = a;
            if (b > maxNode) maxNode = b;
        }
    }
    fclose(f);

    if (E == 0 || maxNode < 0) return 0;

    int *u = (int*)malloc(sizeof(int) * E);
    int *v = (int*)malloc(sizeof(int) * E);
    if (!u || !v) { free(u); free(v); return 0; }

    f = fopen(path, "r");
    if (!f) { perror("fopen"); free(u); free(v); return 0; }

    size_t idx = 0;
    while (idx < E && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        int a, b;
        if (sscanf(line, "%d %d", &a, &b) == 2) {
            u[idx] = a;
            v[idx] = b;
            idx++;
        }
    }
    fclose(f);

    out->u = u;
    out->v = v;
    out->E = idx;
    out->maxNode = maxNode;
    return 1;
}

static int build_graph_csr(const EdgeList *edges, size_t useE, GraphCSR *g) {
    if (useE > edges->E) useE = edges->E;
    int n = edges->maxNode + 1;

    size_t *deg = (size_t*)calloc((size_t)n, sizeof(size_t));
    size_t *out_start = (size_t*)malloc(sizeof(size_t) * ((size_t)n + 1));
    int *out_adj = (int*)malloc(sizeof(int) * useE);
    if (!deg || !out_start || !out_adj) {
        free(deg); free(out_start); free(out_adj);
        return 0;
    }

    for (size_t i = 0; i < useE; i++) {
        int u = edges->u[i];
        if (u >= 0 && u < n) deg[u]++;
    }

    out_start[0] = 0;
    for (int i = 0; i < n; i++) out_start[i + 1] = out_start[i] + deg[i];

    size_t *next_pos = (size_t*)malloc(sizeof(size_t) * (size_t)n);
    if (!next_pos) { free(deg); free(out_start); free(out_adj); return 0; }
    for (int i = 0; i < n; i++) next_pos[i] = out_start[i];

    for (size_t i = 0; i < useE; i++) {
        int u = edges->u[i];
        int v = edges->v[i];
        if (u < 0 || u >= n) continue;
        size_t pos = next_pos[u]++;
        out_adj[pos] = v;
    }

    free(deg);
    free(next_pos);

    g->n = n;
    g->E = useE;
    g->out_start = out_start;
    g->out_adj = out_adj;
    return 1;
}

static void free_graph(GraphCSR *g) {
    free(g->out_start);
    free(g->out_adj);
    g->out_start = NULL;
    g->out_adj = NULL;
}

static void set_affinity(int tid) {
    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1) cpu_count = 1;
    int core = tid % cpu_count;

    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(core, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cs);
}

/* ---------- Parallel BFS V3: affinity + static blocks ---------- */

typedef struct {
    const GraphCSR *g;
    atomic_int *dist;

    const int *frontier;
    size_t frontier_size;
    int level;

    size_t chunk;
    int tid;
    int T;

    int *local_next;
    size_t local_sz;
    size_t local_cap;
} ThreadArg;

static void local_push(ThreadArg *A, int v) {
    if (A->local_sz == A->local_cap) {
        size_t newcap = (A->local_cap == 0) ? 4096 : (A->local_cap * 2);
        int *tmp = (int*)realloc(A->local_next, sizeof(int) * newcap);
        if (!tmp) return;
        A->local_next = tmp;
        A->local_cap = newcap;
    }
    A->local_next[A->local_sz++] = v;
}

static void* bfs_worker_v3(void *arg) {
    ThreadArg *A = (ThreadArg*)arg;
    set_affinity(A->tid);

    const GraphCSR *g = A->g;

    // static scheduling: each thread handles block_id = tid, tid+T, ...
    size_t blocks = (A->frontier_size + A->chunk - 1) / A->chunk;

    for (size_t b = (size_t)A->tid; b < blocks; b += (size_t)A->T) {
        size_t start = b * A->chunk;
        size_t end = start + A->chunk;
        if (end > A->frontier_size) end = A->frontier_size;

        for (size_t i = start; i < end; i++) {
            int u = A->frontier[i];
            size_t a = g->out_start[u];
            size_t c = g->out_start[u + 1];

            for (size_t k = a; k < c; k++) {
                int v = g->out_adj[k];
                if (v < 0 || v >= g->n) continue;

                int expected = -1;
                if (atomic_compare_exchange_strong(&A->dist[v], &expected, A->level + 1)) {
                    local_push(A, v);
                }
            }
        }
    }

    return NULL;
}

static void bfs_parallel_v3(const GraphCSR *g, int T, size_t chunk,
                           int *reachable_out, int *maxdist_out, int **levels_out) {
    atomic_int *dist = (atomic_int*)malloc(sizeof(atomic_int) * (size_t)g->n);
    if (!dist) { *reachable_out = 0; *maxdist_out = 0; *levels_out = NULL; return; }

    for (int i = 0; i < g->n; i++) atomic_init(&dist[i], -1);
    atomic_store(&dist[SOURCE_NODE], 0);

    int *frontier = (int*)malloc(sizeof(int) * (size_t)g->n);
    int *next_frontier = (int*)malloc(sizeof(int) * (size_t)g->n);
    if (!frontier || !next_frontier) {
        free(dist); free(frontier); free(next_frontier);
        *reachable_out = 0; *maxdist_out = 0; *levels_out = NULL; return;
    }

    size_t fsz = 1;
    frontier[0] = SOURCE_NODE;
    int level = 0;

    int level_cap = 64;
    int *level_counts = (int*)calloc((size_t)level_cap, sizeof(int));
    if (!level_counts) {
        free(dist); free(frontier); free(next_frontier);
        *reachable_out = 0; *maxdist_out = 0; *levels_out = NULL; return;
    }
    level_counts[0] = 1;

    while (fsz > 0) {
        pthread_t *th = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)T);
        ThreadArg *A = (ThreadArg*)calloc((size_t)T, sizeof(ThreadArg));

        for (int t = 0; t < T; t++) {
            A[t].g = g;
            A[t].dist = dist;
            A[t].frontier = frontier;
            A[t].frontier_size = fsz;
            A[t].level = level;
            A[t].chunk = chunk;
            A[t].tid = t;
            A[t].T = T;
            A[t].local_next = NULL;
            A[t].local_sz = 0;
            A[t].local_cap = 0;

            pthread_create(&th[t], NULL, bfs_worker_v3, &A[t]);
        }

        for (int t = 0; t < T; t++) pthread_join(th[t], NULL);

        // merge thread-local buffers
        size_t nsz = 0;
        for (int t = 0; t < T; t++) {
            for (size_t i = 0; i < A[t].local_sz; i++) {
                next_frontier[nsz++] = A[t].local_next[i];
            }
        }

        if (nsz > 0) {
            if (level + 1 >= level_cap) {
                int newcap = level_cap * 2;
                int *tmp = (int*)realloc(level_counts, sizeof(int) * (size_t)newcap);
                if (tmp) {
                    for (int i = level_cap; i < newcap; i++) tmp[i] = 0;
                    level_counts = tmp;
                    level_cap = newcap;
                }
            }
            level_counts[level + 1] = (int)nsz;
        }

        for (int t = 0; t < T; t++) free(A[t].local_next);
        free(A);
        free(th);

        // swap
        int *tmpF = frontier;
        frontier = next_frontier;
        next_frontier = tmpF;

        fsz = nsz;
        level++;
    }

    int maxdist = level - 1;
    int reachable = 0;
    for (int k = 0; k <= maxdist; k++) reachable += level_counts[k];

    int *final_levels = (int*)malloc(sizeof(int) * (size_t)(maxdist + 1));
    for (int k = 0; k <= maxdist; k++) final_levels[k] = level_counts[k];

    free(level_counts);
    free(dist);
    free(frontier);
    free(next_frontier);

    *reachable_out = reachable;
    *maxdist_out = maxdist;
    *levels_out = final_levels;
}

/* ---------- CSV writer ---------- */
static void append_result_csv(const char *csvpath, int version, int threads,
                             size_t chunk, int trial, double ms) {
    FILE *f = fopen(csvpath, "r");
    int exists = (f != NULL);
    if (f) fclose(f);

    f = fopen(csvpath, "a");
    if (!f) return;

    if (!exists) fprintf(f, "task,version,threads,chunk,trial,time_ms\n");
    fprintf(f, "task3,%d,%d,%zu,%d,%.3f\n", version, threads, chunk, trial, ms);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <web-Google.txt> <threads> <chunk_frontier>\n", argv[0]);
        fprintf(stderr, "Example: %s web-Google.txt 4 2048\n", argv[0]);
        return 1;
    }

    ensure_results_dir();

    const char *path = argv[1];
    int T = atoi(argv[2]);
    size_t chunk = (size_t)atoll(argv[3]);
    if (T < 1) T = 1;
    if (chunk < 1) chunk = 1;

    EdgeList edges;
    memset(&edges, 0, sizeof(edges));
    if (!read_edges(path, &edges)) return 1;

    size_t Etotal = edges.E;
    size_t Esizes[3];
    Esizes[0] = (size_t)(Etotal * 0.10); if (Esizes[0] < 1) Esizes[0] = 1;
    Esizes[1] = (size_t)(Etotal * 0.50); if (Esizes[1] < 1) Esizes[1] = 1;
    Esizes[2] = Etotal;

    int full_reachable = 0, full_maxdist = 0;
    int *full_levels = NULL;

    for (int si = 0; si < 3; si++) {
        size_t useE = Esizes[si];

        GraphCSR g;
        memset(&g, 0, sizeof(g));
        if (!build_graph_csr(&edges, useE, &g)) {
            free(edges.u); free(edges.v);
            return 1;
        }

        for (int trial = 1; trial <= 5; trial++) {
            int reachable = 0, maxdist = 0;
            int *levels = NULL;

            double t0 = now_ms();
            bfs_parallel_v3(&g, T, chunk, &reachable, &maxdist, &levels);
            double t1 = now_ms();

            append_result_csv("results/task3.csv", 3, T, chunk, trial, t1 - t0);
            free(levels);
        }

        if (si == 2) {
            bfs_parallel_v3(&g, T, chunk, &full_reachable, &full_maxdist, &full_levels);
        }

        free_graph(&g);
    }

    printf("SOURCE %d\n", SOURCE_NODE);
    printf("REACHABLE %d\n", full_reachable);
    printf("MAXDIST %d\n", full_maxdist);
    for (int k = 0; k <= full_maxdist; k++) {
        printf("%d %d\n", k, full_levels[k]);
    }

    free(full_levels);
    free(edges.u);
    free(edges.v);
    return 0;
}
