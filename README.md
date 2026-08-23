# Multithreading

Parallel and distributed computing coursework (PDC) implementing three performance-critical workloads in **C with POSIX threads (pthreads)**. Each task ships three progressive versions-serial baseline, multi-threaded (OS scheduling), and multi-threaded with **CPU affinity + static scheduling**-plus `gprof` profiles and counter/result artifacts.

**Author:** Mohammad Rohaan · **Roll:** 22I-2327 · **Section:** F

---

## Overview

This repository revisits multithreading through workloads with different compute and memory characteristics:

| Task | Workload | Primary dataset |
|------|----------|-----------------|
| **Task 1** | Logistic-regression style inference (18 features + bias) | SUSY CSV (`supersymmetry_dataset.csv` / `SUSY.zip`) |
| **Task 2** | Sparse matrix-vector multiplication (COO → CSR SpMV) | `webbase-1M.mtx` |
| **Task 3** | Graph BFS (edge list → CSR adjacency) | `web-Google.txt` |

For every task, compare **runtime, speedup, and efficiency** across thread counts and chunk sizes, then reason about gains/losses (cache effects, load balance, affinity).

---

## Features

- **V1 (serial):** Single-threaded baseline with timing via `clock_gettime(CLOCK_MONOTONIC)`.
- **V2 (pthreads):** Multi-threaded parallelization with default OS scheduling.
- **V3 (affinity):** Pthreads + `pthread_setaffinity_np` / static chunk scheduling for controlled core placement.
- **Instrumentation:** Per-version counter dumps under `results/`, Excel summaries, and `gprof_*_v3.txt` profiles.
- **Build notes:** `Compilation_Execution Commands.txt` documents compile/run recipes and result CSV checks.

---

## Repository structure

```text
Multithreading/
├── 22I-2327_F_Task1/
│   ├── Task1_v1.c | Task1_v2.c | Task1_v3.c
│   ├── gprof_task1_v3.txt
│   └── results/          # counters + task1.xlsx
├── 22I-2327_F_Task2/
│   ├── Task2_v1.c | Task2_v2.c | Task2_v3.c
│   ├── webbase-1M.mtx
│   ├── gprof_task2_v3.txt
│   └── results/
├── 22I-2327_F_Task3/
│   ├── Task3_v1.c | Task3_v2.c | Task3_v3.c
│   ├── web-Google.txt
│   ├── gprof_task3_v3.txt
│   └── results/
├── Compilation_Execution Commands.txt
├── Assignment Task 1.pdf / Assignment_1_PDC_Spring_2026.pdf
├── SUSY.zip / web-Google.txt.gz / webbase-1M.tar.gz
└── 22I-2327_Sec_F.pdf
```

---

## Build / run

Requires a Linux/WSL environment with `gcc`, pthread support, and (optionally) `gprof`.

### Task 1 - SUSY logistic inference

```bash
cd 22I-2327_F_Task1
mkdir -p results

gcc Task1_v1.c -O2 -lm -o Task1_v1
gcc Task1_v2.c -O2 -lm -pthread -o Task1_v2
gcc Task1_v3.c -O2 -lm -pthread -o Task1_v3

./Task1_v1 supersymmetry_dataset.csv
./Task1_v2 supersymmetry_dataset.csv <threads> <chunk_records>
./Task1_v3 supersymmetry_dataset.csv <threads> <chunk_records>
# e.g. ./Task1_v3 supersymmetry_dataset.csv 4 200000
```

### Task 2 - SpMV (`webbase-1M.mtx`)

```bash
cd 22I-2327_F_Task2
gcc Task2_v1.c -O2 -lm -o Task2_v1
gcc Task2_v2.c -O2 -lm -pthread -o Task2_v2
gcc Task2_v3.c -O2 -lm -pthread -o Task2_v3

./Task2_v1 webbase-1M.mtx
./Task2_v2 webbase-1M.mtx <threads> <chunk_rows>
./Task2_v3 webbase-1M.mtx <threads> <chunk_rows>
# e.g. ./Task2_v3 webbase-1M.mtx 4 1024
```

### Task 3 - BFS (`web-Google.txt`)

```bash
cd 22I-2327_F_Task3
gcc Task3_v1.c -O2 -o Task3_v1
gcc Task3_v2.c -O2 -pthread -o Task3_v2
gcc Task3_v3.c -O2 -pthread -o Task3_v3

./Task3_v1 web-Google.txt
./Task3_v2 web-Google.txt <threads> <chunk>
./Task3_v3 web-Google.txt <threads> <chunk>
```

### Profiling (example)

```bash
gcc Task1_v3.c -O2 -pg -lm -pthread -o Task1_v3_gprof
./Task1_v3_gprof supersymmetry_dataset.csv 4 200000
gprof ./Task1_v3_gprof gmon.out > gprof_task1_v3.txt
```

See `Compilation_Execution Commands.txt` for the full command matrix used in experiments.

---

## Usage notes

- Extract large archives (`SUSY.zip`, `webbase-1M.tar.gz`, `web-Google.txt.gz`) before running.
- Prefer clean `results/` between experiment batches to avoid mixed CSVs.
- Task 1 loads the full CSV into memory (labels + 18 float features); ensure enough RAM for SUSY-scale data.
- Task 2 reads Matrix Market coordinate format, builds CSR, then runs SpMV.
- Task 3 builds CSR adjacency from the SNAP-style edge list and BFS from a fixed source (node `0` in V1).

---

## Extending

- Sweep thread counts (1...N cores) and chunk sizes; plot speedup/efficiency against V1.
- Stick to pthreads if matching the original assignment constraints (no OpenMP/TBB/`std::thread`).
- Export `results/*.xlsx` into a shared notebook for cross-task comparison.
- Replace hardcoded weights/bias in Task 1 with a learned model or additional datasets.

---

## License

Coursework repository - use and share according to your institution's academic integrity policies.
