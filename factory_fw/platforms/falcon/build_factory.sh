#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:-build/falcon}"
OUTPUT_DIR="${BUILD_DIR}/factory_package"

echo "[build_factory] Packaging firmware from $BUILD_DIR"

mkdir -p "$OUTPUT_DIR/bin"
mkdir -p "$OUTPUT_DIR/conf"

# 产测主二进制
cp -f "$BUILD_DIR/factory_test" "$OUTPUT_DIR/bin/"

# 平台配置
cp -f "platforms/falcon/platform.json" "$OUTPUT_DIR/conf/"
cp -f "platforms/falcon/tests.json"    "$OUTPUT_DIR/conf/"

# 动态库（如有）
if [ -f "third_party/mosquitto/lib/libmosquitto.so.1" ]; then
    mkdir -p "$OUTPUT_DIR/lib"
    cp -f "third_party/mosquitto/lib/libmosquitto.so.1" "$OUTPUT_DIR/lib/"
    ln -sf libmosquitto.so.1 "$OUTPUT_DIR/lib/libmosquitto.so"
fi

# 打 tar 包
tar czf "${BUILD_DIR}/factory_firmware.tar.gz" -C "$OUTPUT_DIR" .

echo "[build_factory] Done: ${BUILD_DIR}/factory_firmware.tar.gz"
