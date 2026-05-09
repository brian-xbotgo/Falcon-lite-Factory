#!/bin/bash
# build_factory.sh - 产测固件打包脚本（自包含，不依赖父项目）
#
# 用法:
#   export SDK_DIR=/path/to/rockchip_sdk
#   ./build_factory.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}"   # 所有依赖已在 Falcon_Air_F 内自包含

if [ -z "$SDK_DIR" ]; then
    echo "ERROR: SDK_DIR environment variable is undefined"
    exit 1
fi

sdk_dir=${SDK_DIR%/}

# --- 目标目录设置 ---
target_dir=$sdk_dir/device/rockchip/common/extra-parts/oem/normal/usr
userdata_dir=$sdk_dir/device/rockchip/common/extra-parts/userdata/testdata
rootfs_dst_dir=$sdk_dir/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/target/
rootfs_src_target=$SCRIPT_DIR/xbotgo_rootfs/

# SDK 子目录更新辅助函数
update_sdk_sub_dir()
{
	local src="$SCRIPT_DIR/sdk_patch/$1"
	local dst="$sdk_dir/$2"
	rsync -av "$src/" "$dst/"
}

echo ">>> 产测固件打包模式 <<<"

rm -rf "$target_dir"
rm -rf "$userdata_dir"

mkdir -p "$target_dir"
mkdir -p "$target_dir/device_data/"
mkdir -p "$target_dir"/{bin,lib,conf,scripts}

rm -rf "$rootfs_dst_dir"{lib/modules,lib/firmware,lib/firmware/qca}
mkdir -p "$rootfs_dst_dir"{lib/modules,lib/firmware,lib/firmware/qca}

mkdir "$userdata_dir"

# --- WiFi ---
cp -f "$PROJECT_ROOT/btwifi/confs/wps_hostapd_aic.conf" "$target_dir/conf/wps_hostapd.conf"
cp -f "$PROJECT_ROOT/btwifi/scripts/dragonfly_wifi_init.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/btwifi/scripts/dragonfly_bt_init.sh" "$target_dir/scripts/"

# Build standalone BLE factory advertiser (minimal, no xbot_log/GATT/WiFi deps)
echo ">>> Building ble_factory_advertise..."
make -C "$SCRIPT_DIR/src/ble" \
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-buildroot-linux-gnu-}" \
    SDK_DIR="$sdk_dir" \
    2>&1 | tail -5
cp -f "$SCRIPT_DIR/src/ble/ble_factory_advertise" "$target_dir/bin/"

# --- Mosquitto ---
cp -f "$PROJECT_ROOT/commons/mosquitto/out/sbin/mosquitto" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/bin/mosquitto_pub" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/bin/mosquitto_sub" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/lib/libmosquitto.so.1" "$rootfs_dst_dir/usr/lib/"
cp -f "$PROJECT_ROOT/commons/mosquitto/mosquitto.conf" "$rootfs_dst_dir/etc/"

# --- Factory test binary ---
cp -f "$SCRIPT_DIR/build/Falcon_Air_Factory" "$target_dir/bin/"

# --- LVGL resources (fonts, images for factory display) ---
mkdir -p "$target_dir/conf/lvgl_source/"
cp -rf "$SCRIPT_DIR/firmware/lvgl_source/"* "$target_dir/conf/lvgl_source/"

# --- Stress test tools (产测专属) ---
cp -f "$SCRIPT_DIR/firmware/stress/gpu_test" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/rknn_matmul_api_demo" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/stress" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/rwcheck" "$target_dir/bin/"

# --- Motor driver ---
cp -f "$SCRIPT_DIR/sdk_patch/kernel_patch/motor_tmi8152/motor_tmi8152.ko" "$target_dir/conf/"

# --- Scripts ---
cp -f "$PROJECT_ROOT/lunch/get_cpuinfo.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/insmod_spi_motor.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/power_off.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/start_mqtt.sh" "$target_dir/scripts/"
cp -f "$SCRIPT_DIR/firmware/iperf3_server_safe.sh" "$target_dir/scripts/"
cp -f "$SCRIPT_DIR/usb_gadget/usb_config.sh" "$target_dir/scripts/"

# Use factory lunch script as the boot entry point
cp -f "$SCRIPT_DIR/firmware/Dragonfly_lunch_factory.sh" "$target_dir/scripts/Dragonfly_lunch.sh"

# --- Hall test config ---
cp -f "$SCRIPT_DIR/configs/hall_threshold.ini" "$userdata_dir/"
echo ">>> Copied hall_threshold.ini to $userdata_dir"

# --- Factory mode flag ---
echo "" > "$userdata_dir/factory_mode"
echo "" > "$userdata_dir/aging_time.conf"

chmod -R 777 "$target_dir"/{bin,lib,conf,scripts}
chmod 755 "$rootfs_dst_dir/etc/init.d/"*

# --- xbotgo_rootfs overlay ---
cp "$rootfs_src_target/etc/"* "$rootfs_dst_dir/etc/" -rf
cp "$SCRIPT_DIR/usb_gadget/init_usb_gadget.sh" "$target_dir/scripts/init_usb_gadget.sh"
cp "$SCRIPT_DIR/usb_gadget/usb_config.sh"      "$target_dir/scripts/usb_config.sh"
cp -rf "$rootfs_src_target/usr/"* "$rootfs_dst_dir/usr/"
cp "$rootfs_src_target/etc/profile" "$rootfs_dst_dir/etc/" -rf
cp "$rootfs_src_target/etc/resolv.conf.tail" "$rootfs_dst_dir/etc/" -rf

# --- SDK: kernel + rkbin ---
update_sdk_sub_dir rkbin rkbin
update_sdk_sub_dir kernel kernel


# Force DWC3 to peripheral mode for factory firmware.
# In OTG mode the controller relies on extcon (USB2 PHY ID-pin
# detection) to decide host vs device.  When the PHY ID-pin
# interrupt is unreliable the controller may stay in host mode
# and never create the UDC, which breaks ADB.  Peripheral mode
# unconditionally creates the UDC.
DTS_DIR="$sdk_dir/kernel/arch/arm64/boot/dts/rockchip"
for dts in "$DTS_DIR/rv1126b-evb2-v10.dts" \
	   "$DTS_DIR/rv1126b-evb2-v10.dtsi" \
	   "$DTS_DIR/rv1126b-evb.dtsi"; do
	[ -f "$dts" ] && sed -i 's/dr_mode = "otg"/dr_mode = "peripheral"/g' "$dts"
done

cd "$sdk_dir"
./build.sh kernel

# Enable RNDIS gadget for factory test (usb0 Ethernet over USB)
KERNEL_CFG="$sdk_dir/kernel/.config"
if [ -f "$KERNEL_CFG" ] && ! grep -q "CONFIG_USB_CONFIGFS_RNDIS=y" "$KERNEL_CFG"; then
    echo "CONFIG_USB_CONFIGFS_RNDIS=y" >> "$KERNEL_CFG"
    make -C "$sdk_dir/kernel" olddefconfig -j$(nproc) 2>/dev/null
    # Force rebuild: touch a DTS so make picks up the config change
    touch "$DTS_DIR/rv1126b-evb2-v10.dts"
    make -C "$sdk_dir/kernel" -j$(nproc) 2>&1 | tail -5
    echo ">>> Kernel rebuilt with RNDIS support"
fi

./build.sh

echo ">>> 产测固件打包完成 <<<"
