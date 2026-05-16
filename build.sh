#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

cd "$SCRIPT_DIR"

# Configure if the build directory doesn't exist yet
if [ ! -d "$BUILD_DIR" ]; then
    echo "==> Configuring…"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi

echo "==> Building…"
cmake --build build -j"$(sysctl -n hw.logicalcpu)"

echo ""
echo "Done. Run with:"
echo "  ./build/test-taker"
