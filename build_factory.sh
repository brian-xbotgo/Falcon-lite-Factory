#!/bin/bash
# build_factory.sh - 产测固件打包脚本
#
# 将工厂测试固件打包到 Rockchip SDK 中并触发内核/固件构建。
# 共享依赖 (btwifi, mosquitto, lvgl_app, sdk_patch, xbotgo_rootfs, lunch helpers)
# 从 $PROJECT_ROOT 引用；产测专属资产从本脚本的 firmware/ 子目录引用。
#
# 用法:
#   export SDK_DIR=/path/to/rockchip_sdk
#   export PROJECT_ROOT=/path/to/XbotGo-Falcon-Air-Embedded  # 可选，默认上级目录
#   ./build_factory.sh
#
# 也可以通过父项目的 install.sh factory 调用:
#   ./install.sh factory

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(dirname "$SCRIPT_DIR")}"

if [ -z "$SDK_DIR" ]; then
    echo "ERROR: SDK_DIR environment variable is undefined"
    exit 1
fi

sdk_dir=${SDK_DIR%/}

# --- 目标目录设置 (与 install.sh 保持一致) ---
target_dir=$sdk_dir/device/rockchip/common/extra-parts/oem/normal/usr
userdata_dir=$sdk_dir/device/rockchip/common/extra-parts/userdata/normal
rootfs_dst_dir=$sdk_dir/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/target/
rootfs_src_target=$PROJECT_ROOT/xbotgo_rootfs/

# SDK 子目录更新辅助函数
update_sdk_sub_dir()
{
	local src="$PROJECT_ROOT/sdk_patch/$1"
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

# --- Mosquitto ---
cp -f "$PROJECT_ROOT/commons/mosquitto/out/sbin/mosquitto" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/bin/mosquitto_pub" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/bin/mosquitto_sub" "$rootfs_dst_dir/usr/bin/"
cp -f "$PROJECT_ROOT/commons/mosquitto/out/lib/libmosquitto.so.1" "$rootfs_dst_dir/usr/lib/"
cp -f "$PROJECT_ROOT/commons/mosquitto/mosquitto.conf" "$rootfs_dst_dir/etc/"

# --- Factory test binary ---
cp -f "$SCRIPT_DIR/build/Falcon_Air_Factory" "$target_dir/bin/"

# --- LVGL apps + resources (for charge/battery display) ---
cp -f "$PROJECT_ROOT/lvgl_app/charge_lvgl_app/charge_lvgl_app" "$target_dir/bin/"
cp -f "$PROJECT_ROOT/lvgl_app/normal_lvgl_app/normal_lvgl_app" "$target_dir/bin/"
mkdir -p "$target_dir/conf/lvgl_source/"
cp -rf "$PROJECT_ROOT/lvgl_app/source/"* "$target_dir/conf/lvgl_source/"

# --- Stress test tools (产测专属) ---
cp -f "$SCRIPT_DIR/firmware/stress/gpu_test" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/rknn_matmul_api_demo" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/stress" "$target_dir/bin/"
cp -f "$SCRIPT_DIR/firmware/stress/rwcheck" "$target_dir/bin/"

# --- Motor driver ---
cp -f "$PROJECT_ROOT/sdk_patch/kernel_patch/motor_tmi8152/motor_tmi8152.ko" "$target_dir/conf/"

# --- Scripts ---
cp -f "$PROJECT_ROOT/lunch/get_cpuinfo.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/insmod_spi_motor.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/power_off.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/lunch/start_mqtt.sh" "$target_dir/scripts/"
cp -f "$SCRIPT_DIR/firmware/iperf3_server_safe.sh" "$target_dir/scripts/"
cp -f "$PROJECT_ROOT/stream/uvc/usb_config.sh" "$target_dir/scripts/"

# Use factory lunch script as the boot entry point
cp -f "$SCRIPT_DIR/firmware/Dragonfly_lunch_factory.sh" "$target_dir/scripts/Dragonfly_lunch.sh"

# --- Factory mode flag ---
echo "" > "$userdata_dir/factory_mode"
echo "" > "$userdata_dir/aging_time.conf"

chmod -R 777 "$target_dir"/{bin,lib,conf,scripts}
chmod 755 "$rootfs_dst_dir/etc/init.d/"*

# --- xbotgo_rootfs overlay ---
cp "$rootfs_src_target/etc/"* "$rootfs_dst_dir/etc/" -rf
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
./build.sh

echo ">>> 产测固件打包完成 <<<"
