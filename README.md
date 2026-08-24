# Multithreading (PDC Assignment 1)

Three **C + POSIX threads** workloads with three versions each: **serial**, **pthreads (OS scheduling)**, and **pthreads + CPU affinity / static chunks**. Measure runtime, speedup, and efficiency; reason about cache and load-balance.

**Author:** Mohammad Rohaan · **Roll:** 22I-2327 · **Section:** F  
**Course:** Parallel and Distributed Computing  
**Allowed:** C/C++ and **Pthreads only** (no OpenMP, TBB, or `std::thread` per `Assignment1.txt`)

---

## Table of contents

1. [Assignment contract](#assignment-contract)
2. [The three workloads](#the-three-workloads)
3. [Task 1 — SUSY logistic inference](#task-1--susy-logistic-inference)
4. [Task 2 — SpMV (COO → CSR)](#task-2--spmv-coo--csr)
5. [Task 3 — BFS on a web graph](#task-3--bfs-on-a-web-graph)
6. [Version matrix](#version-matrix)
7. [Build, run, gprof](#build-run-gprof)
8. [How to write the report](#how-to-write-the-report)
9. [File layout](#file-layout)

---

## Assignment contract

From `Assignment1.txt`:

| Version | Meaning |
|---------|---------|
| **V1** | Single-threaded baseline |
| **V2** | Multi-threaded; **OS** schedules threads (no affinity) |
| **V3** | Multi-threaded + **`pthread_setaffinity_np`** and an explicit static/chunk schedule |

Evaluate **runtime, speedup, efficiency** across thread counts and chunk sizes. Naming example: `ROLL-NUM-SECTION-TASK#-VERSION.EXT`. Folder zip for Classroom; **no binaries** in the official zip.

Timing in the provided Task 1 serial code uses `clock_gettime(CLOCK_MONOTONIC)` converted to **milliseconds**.

---

## The three workloads

| Task | Kernel | Dataset | Why it is different |
|------|--------|---------|---------------------|
| 1 | Dense logistic-style scoring (18 features + bias) | SUSY CSV (`supersymmetry_dataset.csv` / `SUSY.zip`) | Regular, compute-bound, contiguous floats |
| 2 | Sparse matrix–vector multiply | `webbase-1M.mtx` (Matrix Market COO → CSR) | Irregular memory, CSR row chunks |
| 3 | Graph BFS | SNAP `web-Google.txt` | Frontier-based, pointer chasing |

---

## Task 1 — SUSY logistic inference

**Files:** `Task1_v1.c`, `Task1_v2.c`, `Task1_v3.c` (also under `22I-2327_F_Task1/` on GitHub)

Serial source details:

- `#define FEATS 18`
- Fixed **bias** `-0.35` and a length-18 **weight vector** `W[]` (not learned in this file — assignment inference with given coefficients).
- CSV: skip header, parse label then 18 floats per line (`parse_line`).
- Label threshold: `y >= 0.5` → 1 else 0.
- Counters: `pred_pos, tp, fp, tn, fn` for a classification-style dump after scoring every row.
- Load strategy: count lines, allocate, fill `labels[]` and `features[N * 18]`.

**V2/V3** split records into **chunks** (`<threads> <chunk_records>` on the CLI). V3 pins threads to cores.

```bash
./Task1_v1 supersymmetry_dataset.csv
./Task1_v2 supersymmetry_dataset.csv 4 200000
./Task1_v3 supersymmetry_dataset.csv 4 200000
```

SUSY is large — ensure RAM for the full float matrix.

---

## Task 2 — SpMV (COO → CSR)

**Files:** `Task2_v1.c` / `v2` / `v3`  
**Matrix:** `webbase-1M.mtx`

1. Read Matrix Market **coordinate** triples.
2. Build **CSR** (`row_ptr`, `col_idx`, `values`).
3. Compute `y = A x` (SpMV).
4. Parallelize over **row chunks** in V2/V3 (`<threads> <chunk_rows>`).

```bash
./Task2_v1 webbase-1M.mtx
./Task2_v3 webbase-1M.mtx 4 1024
```

Related report-only SpMV work: sibling repo **SPMV_Optimization** (NC project PDFs + `system_architecture.txt` on an i5-5300U / 4-thread host).

---

## Task 3 — BFS on a web graph

**Files:** `Task3_v1.c` / `v2` / `v3`  
**Graph:** `web-Google.txt` (SNAP-style edge list)

1. Build CSR adjacency.
2. BFS from a fixed source (**node `0` in V1**).
3. V2/V3 parallelize a level or chunked neighbor expansion (`<threads> <chunk>`).

```bash
./Task3_v1 web-Google.txt
./Task3_v3 web-Google.txt 4 1024
```

---

## Version matrix

| | V1 | V2 | V3 |
|--|----|----|-----|
| Threads | 1 | argv | argv |
| Scheduling | — | default OS | **affinity** + static chunks |
| Link | `-lm` (T1/T2) | `-pthread -lm` | `-pthread -lm` |
| Profile | optional | optional | `gprof_*_v3.txt` committed for Task 1 |

Artifacts: `results/` counter dumps, Excel summaries, `Compilation_Execution Commands.txt`.

---

## Build, run, gprof

Linux/WSL, `gcc`:

```bash
# Task 1
gcc Task1_v1.c -O2 -lm -o Task1_v1
gcc Task1_v2.c -O2 -lm -pthread -o Task1_v2
gcc Task1_v3.c -O2 -lm -pthread -o Task1_v3

# Task 2
gcc Task2_v1.c -O2 -lm -o Task2_v1
gcc Task2_v2.c -O2 -lm -pthread -o Task2_v2
gcc Task2_v3.c -O2 -lm -pthread -o Task2_v3

# Task 3
gcc Task3_v1.c -O2 -o Task3_v1
gcc Task3_v2.c -O2 -pthread -o Task3_v2
gcc Task3_v3.c -O2 -pthread -o Task3_v3
```

Profiling example:

```bash
gcc Task1_v3.c -O2 -pg -lm -pthread -o Task1_v3_gprof
./Task1_v3_gprof supersymmetry_dataset.csv 4 200000
gprof ./Task1_v3_gprof gmon.out > gprof_task1_v3.txt
```

Extract `SUSY.zip`, `webbase-1M.tar.gz`, `web-Google.txt.gz` first. Sweep threads `1 … nproc` and several chunk sizes; do **not** mix CSVs between batches.

---

## How to write the report

For each task:

1. Table: threads × chunk → time, speedup `T1/Tp`, efficiency `S/p`.
2. Explain **gains** (parallel work, affinity reducing migration) and **losses** (too-small chunks, false sharing, memory bandwidth on SpMV/BFS, Hyper-V 4 logical CPUs).
3. Attach `gprof` hot functions for V3.
4. Keep language/library constraints explicit (pthreads only).

---

## File layout

```text
Multithreading/
├── Assignment1.txt
├── Compilation_Execution Commands.txt
├── Task1_v1.c  Task1_v3.c  gprof_task1_v3.txt  task1_v3_counters.txt
├── Task2_v1.c  Task2_v3.c
├── Task3_v1.c  Task3_v3.c
└── (GitHub) 22I-2327_F_Task{1,2,3}/ + zips + PDFs
```

---

## Author

**Mohammad Rohaan** — 22I-2327 · Section F  
[rohaan2802](https://github.com/rohaan2802)
