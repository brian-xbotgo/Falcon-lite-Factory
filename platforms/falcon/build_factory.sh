#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:-build/falcon}"
OUTPUT_DIR="${BUILD_DIR}/factory_package"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SDK_TARGET_DIR=""

if [ -n "${FALCON_SDK:-}" ]; then
    SDK_TARGET_DIR="$(dirname "$FALCON_SDK")/target"
fi

echo "[build_factory] Packaging firmware from $BUILD_DIR"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/bin" \
         "$OUTPUT_DIR/conf" \
         "$OUTPUT_DIR/etc/dbus-1" \
         "$OUTPUT_DIR/scripts" \
         "$OUTPUT_DIR/init.d" \
         "$OUTPUT_DIR/lib" \
         "$OUTPUT_DIR/libexec/bluetooth" \
         "$OUTPUT_DIR/sdk_patch"

cp -f "$BUILD_DIR/factory_test" "$OUTPUT_DIR/bin/"

cp -f "$SCRIPT_DIR/platform.json" "$OUTPUT_DIR/conf/"
cp -f "$SCRIPT_DIR/tests.json" "$OUTPUT_DIR/conf/"
cp -f "$SCRIPT_DIR/conf/"* "$OUTPUT_DIR/conf/"

cp -f "$SCRIPT_DIR/scripts/"* "$OUTPUT_DIR/scripts/"
cp -f "$SCRIPT_DIR/init.d/"* "$OUTPUT_DIR/init.d/"
cp -f "$SCRIPT_DIR/sdk_patch/"* "$OUTPUT_DIR/sdk_patch/"
chmod 755 "$OUTPUT_DIR/scripts/"*.sh "$OUTPUT_DIR/init.d/"* "$OUTPUT_DIR/sdk_patch/"*.sh

for tool in mosquitto dbus-daemon dbus-uuidgen hciattach hciconfig; do
    if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/bin/$tool" ]; then
        cp -f "$SDK_TARGET_DIR/usr/bin/$tool" "$OUTPUT_DIR/bin/"
    fi
done

if [ -n "$SDK_TARGET_DIR" ] && [ -d "$SDK_TARGET_DIR/etc/dbus-1" ]; then
    cp -a "$SDK_TARGET_DIR/etc/dbus-1/." "$OUTPUT_DIR/etc/dbus-1/"
fi

if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/libexec/bluetooth/bluetoothd" ]; then
    cp -f "$SDK_TARGET_DIR/usr/libexec/bluetooth/bluetoothd" "$OUTPUT_DIR/libexec/bluetooth/"
fi

if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/lib/libmosquitto.so.1" ]; then
    cp -f "$SDK_TARGET_DIR/usr/lib/libmosquitto.so.1" "$OUTPUT_DIR/lib/"
elif [ -f "$FW_ROOT/third_party/mosquitto/lib/libmosquitto.so.1" ]; then
    cp -f "$FW_ROOT/third_party/mosquitto/lib/libmosquitto.so.1" "$OUTPUT_DIR/lib/"
fi
if [ -f "$OUTPUT_DIR/lib/libmosquitto.so.1" ]; then
    ln -sf libmosquitto.so.1 "$OUTPUT_DIR/lib/libmosquitto.so"
fi

for lib in \
    libasound.so.2 \
    libcrypto.so.3 \
    libdbus-1.so.3 \
    libexpat.so.1 \
    libffi.so.8 \
    libgio-2.0.so.0 \
    libglib-2.0.so.0 \
    libgobject-2.0.so.0 \
    libpcre2-8.so.0 \
    librkwifibt.so \
    librockchip_mpp.so.1 \
    libssl.so.3 \
    libstdc++.so.6 \
    libgcc_s.so.1; do
    if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/lib/$lib" ]; then
        cp -f "$SDK_TARGET_DIR/usr/lib/$lib" "$OUTPUT_DIR/lib/"
    fi
done

if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/lib/modules/battery.ko" ]; then
    mkdir -p "$OUTPUT_DIR/lib/modules"
    cp -f "$SDK_TARGET_DIR/usr/lib/modules/battery.ko" "$OUTPUT_DIR/lib/modules/"
fi

FALCON_SOURCE_ROOT="${FALCON_SOURCE_ROOT:-/home/gdh/falcon/app/lastcode20251210/XbotGo-Dragonfly-Embedded}"
MOTOR_KO="$FALCON_SOURCE_ROOT/sdk_patch/kernel_patch/motor_tmi8152/motor_tmi8152.ko"
if [ -f "$MOTOR_KO" ]; then
    cp -f "$MOTOR_KO" "$OUTPUT_DIR/conf/"
fi

tar czf "${BUILD_DIR}/factory_firmware.tar.gz" -C "$OUTPUT_DIR" .

echo "[build_factory] Done: ${BUILD_DIR}/factory_firmware.tar.gz"
