# Datasets

Two of the three assignment datasets live in this folder, as compressed archives:

| File | Workload | Source | Unpacked name |
|------|----------|--------|----------------|
| `webbase-1M.tar.gz` (~11 MB) | Task 2 — sparse matrix–vector multiply | [SuiteSparse: Williams/webbase-1M](https://sparse.tamu.edu/Williams/webbase-1M) | `webbase-1M.mtx` (~70 MB) |
| `web-Google.txt.gz` (~21 MB) | Task 3 — BFS on a web graph | [SNAP web-Google](https://snap.stanford.edu/data/web-Google.html) | `web-Google.txt` (~75 MB) |

Unpack them with:

```bash
bash scripts/extract_datasets.sh
```

or by hand:

```bash
cd datasets
tar -xzf webbase-1M.tar.gz
# the archive contains webbase-1M/webbase-1M.mtx
mv webbase-1M/webbase-1M.mtx ./webbase-1M.mtx
gzip -dk web-Google.txt.gz
```

The unpacked files are listed in `.gitignore` because they can be rebuilt from the archives.

## SUSY (Task 1)

The SUSY archive is about 799 MB, which is over GitHub's 100 MB file limit, so it is not stored in this git tree. It belongs in the [`release/`](../release/README.md) folder and is meant to be attached to a [GitHub Release](https://github.com/rohaan2802/Multithreading/releases). You can also download the same dataset from the [UCI SUSY page](https://archive.ics.uci.edu/dataset/279/susy). After unzipping, pass the CSV path to the Task 1 programs (the file is usually called `SUSY.csv` or `supersymmetry_dataset.csv`).
