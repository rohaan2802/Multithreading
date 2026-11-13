#!/usr/bin/env bash
# Unpack the Task 2 and Task 3 datasets into datasets/.
# The SUSY archive is not in git — see release/README.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DS="$ROOT/datasets"
cd "$DS"

echo "Extracting webbase-1M.tar.gz ..."
tar -xzf webbase-1M.tar.gz
if [ -f webbase-1M/webbase-1M.mtx ]; then
  mv -f webbase-1M/webbase-1M.mtx ./webbase-1M.mtx
  rmdir webbase-1M 2>/dev/null || true
fi
echo "  -> $DS/webbase-1M.mtx"

echo "Extracting web-Google.txt.gz ..."
gzip -dk -f web-Google.txt.gz
echo "  -> $DS/web-Google.txt"

echo "Done. For the SUSY CSV used by Task 1, see release/README.md."
