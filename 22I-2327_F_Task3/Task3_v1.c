/*
NAME: Mohammad Rohaan
ROLL #: 22i-2327
ASSIGNMENT#: 1
FILE: TASK3 V1 (Serial BFS)
*/

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define LINE_BUF 4096
#define SOURCE_NODE 0

typedef struct {
    int *u;
    int *v;
    size_t E;
    int maxNode;
} EdgeList;

typedef struct {
    int n;              // number of nodes = maxNode + 1
    size_t E;           // edges used
    size_t *out_start;  // size n+1
    int *out_adj;       // size E
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

/* Read edge list, ignore lines starting with # */
static int read_edges(const char *path, EdgeList *out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 0; }

    char line[LINE_BUF];

    // 1) Count edges and find max node
    size_t cap = 0;
    size_t E = 0;
    int maxNode = -1;

    // We'll first count edges by scanning file
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

    if (E == 0 || maxNode < 0) {
        fprintf(stderr, "No edges found.\n");
        return 0;
    }

    // Allocate arrays
    cap = E;
    int *u = (int*)malloc(sizeof(int) * cap);
    int *v = (int*)malloc(sizeof(int) * cap);
    if (!u || !v) {
        fprintf(stderr, "Memory allocation failed for edges.\n");
        free(u); free(v);
        return 0;
    }

    // 2) Read again and fill arrays
    f = fopen(path, "r");
    if (!f) { perror("fopen"); free(u); free(v); return 0; }

    size_t idx = 0;
    while (idx < cap && fgets(line, sizeof(line), f)) {
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

/* Build CSR adjacency for first useE edges (deterministic subset = first useE edges) */
static int build_graph_csr(const EdgeList *edges, size_t useE, GraphCSR *g) {
    if (useE > edges->E) useE = edges->E;

    int n = edges->maxNode + 1;
    size_t *deg = (size_t*)calloc((size_t)n, sizeof(size_t));
    size_t *out_start = (size_t*)malloc(sizeof(size_t) * ((size_t)n + 1));
    int *out_adj = (int*)malloc(sizeof(int) * useE);

    if (!deg || !out_start || !out_adj) {
        fprintf(stderr, "Memory allocation failed for graph CSR.\n");
        free(deg); free(out_start); free(out_adj);
        return 0;
    }

    // count out-degree
    for (size_t i = 0; i < useE; i++) {
        int u = edges->u[i];
        if (u >= 0 && u < n) deg[u]++;
    }

    // prefix sum into out_start
    out_start[0] = 0;
    for (int i = 0; i < n; i++) {
        out_start[i + 1] = out_start[i] + deg[i];
    }

    size_t *next_pos = (size_t*)malloc(sizeof(size_t) * (size_t)n);
    if (!next_pos) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(deg); free(out_start); free(out_adj);
        return 0;
    }

    for (int i = 0; i < n; i++) next_pos[i] = out_start[i];

    // fill adjacency
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

/* BFS from SOURCE_NODE, compute reachable/maxdist/level counts */
static void bfs_serial(const GraphCSR *g, int **dist_out, int *reachable_out, int *maxdist_out, int **level_counts_out) {
    int n = g->n;

    int *dist = (int*)malloc(sizeof(int) * (size_t)n);
    int *queue = (int*)malloc(sizeof(int) * (size_t)n);

    if (!dist || !queue) {
        fprintf(stderr, "Memory allocation failed for BFS arrays.\n");
        free(dist); free(queue);
        *dist_out = NULL;
        *level_counts_out = NULL;
        *reachable_out = 0;
        *maxdist_out = 0;
        return;
    }

    for (int i = 0; i < n; i++) dist[i] = -1;

    int front = 0, back = 0;
    dist[SOURCE_NODE] = 0;
    queue[back++] = SOURCE_NODE;

    int reachable = 0;
    int maxdist = 0;

    // BFS
    while (front < back) {
        int u = queue[front++];
        reachable++;

        int du = dist[u];
        if (du > maxdist) maxdist = du;

        size_t start = g->out_start[u];
        size_t end = g->out_start[u + 1];

        for (size_t k = start; k < end; k++) {
            int v = g->out_adj[k];
            if (v < 0 || v >= n) continue;
            if (dist[v] == -1) {
                dist[v] = du + 1;
                queue[back++] = v;
            }
        }
    }

    // level counts
    int *level_counts = (int*)calloc((size_t)maxdist + 1, sizeof(int));
    if (!level_counts) {
        fprintf(stderr, "Memory allocation failed for level counts.\n");
        free(dist); free(queue);
        *dist_out = NULL;
        *level_counts_out = NULL;
        *reachable_out = 0;
        *maxdist_out = 0;
        return;
    }

    for (int i = 0; i < n; i++) {
        int d = dist[i];
        if (d >= 0) level_counts[d]++;
    }

    free(queue);

    *dist_out = dist;
    *reachable_out = reachable;
    *maxdist_out = maxdist;
    *level_counts_out = level_counts;
}

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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <web-Google.txt>\n", argv[0]);
        return 1;
    }

    ensure_results_dir();

    EdgeList edges;
    memset(&edges, 0, sizeof(edges));

    if (!read_edges(argv[1], &edges)) return 1;

    // Subsets by edges: first 10%, first 50%, full
    size_t Etotal = edges.E;
    size_t Esizes[3];
    Esizes[0] = (size_t)(Etotal * 0.10); if (Esizes[0] < 1) Esizes[0] = 1;
    Esizes[1] = (size_t)(Etotal * 0.50); if (Esizes[1] < 1) Esizes[1] = 1;
    Esizes[2] = Etotal;

    // chunk for Task3: frontier elements per chunk (V1 has no chunk scheduling)
    // We'll log a fixed value for report consistency.
    size_t chunk_frontier = 1024;

    int full_reachable = 0, full_maxdist = 0;
    int *full_levels = NULL;

    for (int si = 0; si < 3; si++) {
        size_t useE = Esizes[si];

        GraphCSR g;
        memset(&g, 0, sizeof(g));

        // Build adjacency outside timed region (allowed)
        if (!build_graph_csr(&edges, useE, &g)) {
            free(edges.u); free(edges.v);
            return 1;
        }

        int *dist = NULL;
        int reachable = 0, maxdist = 0;
        int *levels = NULL;

        // Timed region: BFS only
        for (int trial = 1; trial <= 5; trial++) {
            double t0 = now_ms();
            bfs_serial(&g, &dist, &reachable, &maxdist, &levels);
            double t1 = now_ms();

            double ms = t1 - t0;
            append_result_csv("results/task3.csv", 1, 1, chunk_frontier, trial, ms);

            free(dist);
            free(levels);
        }

        // For full dataset, compute once more and keep results for printing
        if (si == 2) {
            bfs_serial(&g, &dist, &full_reachable, &full_maxdist, &full_levels);
            free(dist);
        }

        free_graph(&g);
    }

    // Required output for full dataset
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
