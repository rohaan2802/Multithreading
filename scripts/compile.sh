#!/usr/bin/env bash
# Compile all nine programs. Run from the repository root, on Linux or WSL.
set -euo pipefail
cd "$(dirname "$0")/.."
make -j"$(nproc 2>/dev/null || echo 2)"
echo "Binaries are in ./bin"
