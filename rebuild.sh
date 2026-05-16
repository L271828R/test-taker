#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

echo "==> Removing old build directory…"
rm -rf build

echo "==> Configuring…"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "==> Building…"
cmake --build build -j"$(sysctl -n hw.logicalcpu)"

echo ""
echo "Done. Run with:"
echo "  ./build/mdviewer <file.md|file.html>"
