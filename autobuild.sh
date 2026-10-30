#!/bin/bash

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
INCLUDE_DIR="/usr/include/my_muduo"
LIBRARY="$PROJECT_DIR/lib/libmy_muduo.so"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --parallel

sudo install -d "$INCLUDE_DIR"
for header in "$PROJECT_DIR"/*.h; do
    sudo install -m 644 "$header" "$INCLUDE_DIR/"
done
sudo install -m 755 "$LIBRARY" /usr/lib/
sudo ldconfig