#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:-build/falcon}"
echo "[build_factory] Packaging firmware from $BUILD_DIR"
# TODO: implement firmware packaging (copy binary, config files, create image)
echo "[build_factory] Done"
