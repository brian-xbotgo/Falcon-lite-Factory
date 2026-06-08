#!/bin/sh
set -eu

SDK_ROOT="${1:-}"
SOURCE_PATCH_DIR="${2:-${FACTORY_SOURCE_PATCH_DIR:-/home/gdh/falcon/app/lastcode20251210/XbotGo-Dragonfly-Embedded/sdk_patch}}"

if [ -z "$SDK_ROOT" ]; then
    echo "Usage: $0 <Omni3576-sdk-root> [source-sdk-patch-dir]" >&2
    exit 1
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MANIFEST="$SCRIPT_DIR/factory_patch_manifest.txt"
if [ ! -f "$MANIFEST" ]; then
    echo "missing manifest: $MANIFEST" >&2
    exit 1
fi

echo "[factory_patch] SDK_ROOT=$SDK_ROOT"
echo "[factory_patch] SOURCE_PATCH_DIR=$SOURCE_PATCH_DIR"
echo "[factory_patch] minimal patch checklist:"
cat "$MANIFEST"
echo

if [ "${FACTORY_PATCH_DRY_RUN:-0}" = "1" ]; then
    echo "[factory_patch] dry run only"
    exit 0
fi

for dir in kernel_patch buildroot_patch external device device_rk3576_defconfig; do
    apply_script="$SOURCE_PATCH_DIR/$dir/__apply_patch.sh"
    if [ ! -x "$apply_script" ]; then
        echo "[factory_patch] missing or not executable: $apply_script" >&2
        exit 1
    fi
    echo "[factory_patch] applying $dir"
    (cd "$SOURCE_PATCH_DIR/$dir" && SDK_DIR="$SDK_ROOT" ./__apply_patch.sh)
done

echo "[factory_patch] done"
