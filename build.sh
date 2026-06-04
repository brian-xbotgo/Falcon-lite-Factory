#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PLATFORM="${PLATFORM:-falcon}"
PLATFORM_LC=$(echo "$PLATFORM" | tr '[:upper:]' '[:lower:]')
PLATFORM_DIR="$SCRIPT_DIR/platforms/${PLATFORM_LC}"
BUILD_DIR="$SCRIPT_DIR/build/${PLATFORM_LC}"
TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/platforms/${PLATFORM_LC}.cmake"

[[ -d "$PLATFORM_DIR" ]]    || { echo "Error: platform '$PLATFORM' not found" >&2; exit 1; }
[[ -f "$TOOLCHAIN_FILE" ]]  || { echo "Error: toolchain not found: $TOOLCHAIN_FILE" >&2; exit 1; }

cmake -S "$PLATFORM_DIR" -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
      -DFW_ROOT="$SCRIPT_DIR"

cmake --build "$BUILD_DIR" --parallel
"$PLATFORM_DIR/build_factory.sh" "$BUILD_DIR"
