#!/bin/sh
set -eu

SDK_ROOT="${1:-}"

if [ -z "$SDK_ROOT" ]; then
    echo "Usage: $0 <Omni3576-sdk-root> [source-sdk-patch-dir]" >&2
    exit 1
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SOURCE_PATCH_DIR="${2:-${FACTORY_SOURCE_PATCH_DIR:-$SCRIPT_DIR}}"
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

verify_manifest_files()
{
    while IFS= read -r line; do
        case "$line" in
            ""|\#*)
                continue
                ;;
        esac
        if [ ! -f "$SOURCE_PATCH_DIR/$line" ]; then
            echo "[factory_patch] manifest file missing: $SOURCE_PATCH_DIR/$line" >&2
            exit 1
        fi
    done < "$MANIFEST"
}

verify_manifest_files

if [ "${FACTORY_PATCH_DRY_RUN:-0}" = "1" ]; then
    echo "[factory_patch] dry run only"
    exit 0
fi

copy_file()
{
    copy_src="$1"
    copy_dst="$2"

    if [ ! -f "$copy_src" ]; then
        echo "[factory_patch] missing file: $copy_src" >&2
        exit 1
    fi

    mkdir -p "$(dirname "$copy_dst")"
    cp -f "$copy_src" "$copy_dst"
    echo "[factory_patch] copied $(basename "$copy_src") -> $copy_dst"
}

apply_patch_file()
{
    patch_file="$1"
    patch_workdir="$2"

    if [ ! -f "$patch_file" ]; then
        echo "[factory_patch] missing patch: $patch_file" >&2
        exit 1
    fi

    if git -C "$patch_workdir" apply --check "$patch_file" >/dev/null 2>&1; then
        git -C "$patch_workdir" apply "$patch_file"
        echo "[factory_patch] applied $(basename "$patch_file")"
        return 0
    fi

    if git -C "$patch_workdir" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        echo "[factory_patch] already applied $(basename "$patch_file")"
        return 0
    fi

    case "$(basename "$patch_file")" in
        driver_media_i2c_Makefile.patch)
            if grep -q 'obj-y += gc4663.o' "$patch_workdir/drivers/media/i2c/Makefile" &&
               grep -q 'obj-y += imx678.o' "$patch_workdir/drivers/media/i2c/Makefile"; then
                echo "[factory_patch] patch result already present $(basename "$patch_file")"
                return 0
            fi
            ;;
    esac

    echo "[factory_patch] failed to apply $(basename "$patch_file")" >&2
    git -C "$patch_workdir" apply --check "$patch_file" >&2
    exit 1
}

apply_kernel_patch()
{
    kernel_src="$SOURCE_PATCH_DIR/kernel_patch"
    kernel_dst="$SDK_ROOT/kernel-6.1"

    echo "[factory_patch] applying kernel patch subset"
    copy_file "$kernel_src/rockchip_linux_defconfig" "$kernel_dst/arch/arm64/configs/rockchip_linux_defconfig"
    for name in dragonfly.dts \
                dragonfly-core3576.dtsi \
                dragonfly-cam.dtsi \
                dragonfly-key.dtsi \
                dragonfly-motor.dtsi \
                dragonfly-pcie.dtsi \
                dragonfly-usb-c.dtsi \
                dragonfly-wireless.dtsi; do
        copy_file "$kernel_src/$name" "$kernel_dst/arch/arm64/boot/dts/rockchip/$name"
    done

    copy_file "$kernel_src/gc4663.c" "$kernel_dst/drivers/media/i2c/gc4663.c"
    copy_file "$kernel_src/imx678.c" "$kernel_dst/drivers/media/i2c/imx678.c"
    copy_file "$kernel_src/fb_nv3007.c" "$kernel_dst/drivers/staging/fbtft/fb_nv3007.c"
    copy_file "$kernel_src/rk_fiq_debugger.c" "$kernel_dst/drivers/soc/rockchip/fiq_debugger/rk_fiq_debugger.c"

    for patch in driver_media_i2c_Makefile.patch \
                 fbtft_nv3007.patch \
                 dragonfly-wifi.patch \
                 allow_connect_two_role.patch \
                 usb_charger_detect.patch \
                 0001-mmc-dw_mmc-prevent-kernel-panic-on-sd-card-errors.patch; do
        apply_patch_file "$kernel_src/$patch" "$kernel_dst"
    done
}

apply_buildroot_patch()
{
    buildroot_src="$SOURCE_PATCH_DIR/buildroot_patch"

    echo "[factory_patch] applying buildroot patch subset"
    copy_file "$buildroot_src/S01syslogd" "$SDK_ROOT/buildroot/output/rockchip_rk3576_ipc/target/etc/init.d/S01syslogd"
    copy_file "$buildroot_src/S01syslogd" "$SDK_ROOT/buildroot/package/busybox/S01syslogd"
    copy_file "$buildroot_src/dragonfly-bt.patch" "$SDK_ROOT/buildroot/package/bluez5_utils/dragonfly-bt.patch"
    copy_file "$buildroot_src/post-build.sh" "$SDK_ROOT/buildroot/board/rockchip/common/post-build.sh"
}

apply_external_patch()
{
    external_src="$SOURCE_PATCH_DIR/external"

    echo "[factory_patch] applying external patch subset"
    copy_file "$external_src/dnsmasq.conf" "$SDK_ROOT/external/rkwifibt/conf/dnsmasq.conf"
    copy_file "$external_src/wpa_supplicant.conf" "$SDK_ROOT/external/rkwifibt/conf/wpa_supplicant.conf"
}

apply_device_patch()
{
    device_src="$SOURCE_PATCH_DIR/device"

    echo "[factory_patch] applying device patch subset"
    copy_file "$device_src/rockchip/common/scripts/mk-extra-parts.sh" \
        "$SDK_ROOT/device/rockchip/common/scripts/mk-extra-parts.sh"
}

apply_device_defconfig_patch()
{
    defconfig_src="$SOURCE_PATCH_DIR/device_rk3576_defconfig"

    echo "[factory_patch] applying device rk3576 defconfig subset"
    copy_file "$defconfig_src/rockchip_rk3576_dragonfly_defconfig" \
        "$SDK_ROOT/device/rockchip/rk3576/rockchip_rk3576_dragonfly_defconfig"
    copy_file "$defconfig_src/rockchip_rk3576_ipc_defconfig" \
        "$SDK_ROOT/buildroot/configs/rockchip_rk3576_ipc_defconfig"
    copy_file "$defconfig_src/parameter-ab.txt" \
        "$SDK_ROOT/device/rockchip/rk3576/parameter-ab.txt"
    copy_file "$defconfig_src/rk3576_defconfig" \
        "$SDK_ROOT/u-boot/configs/rk3576_defconfig"
    copy_file "$defconfig_src/recovery.mk" \
        "$SDK_ROOT/buildroot/package/rockchip/recovery/recovery.mk"
    copy_file "$defconfig_src/ddrbin_param.txt" \
        "$SDK_ROOT/rkbin/tools/ddrbin_param.txt"
    copy_file "$defconfig_src/rk3576_ddr_lp4_2112MHz_lp5_2736MHz_v1.09.bin" \
        "$SDK_ROOT/rkbin/bin/rk35/rk3576_ddr_lp4_2112MHz_lp5_2736MHz_v1.09.bin"
    copy_file "$defconfig_src/rk3576_usbplug_v1.04.bin" \
        "$SDK_ROOT/rkbin/bin/rk35/rk3576_usbplug_v1.04.bin"
}

apply_kernel_patch
apply_buildroot_patch
apply_external_patch
apply_device_patch
apply_device_defconfig_patch

echo "[factory_patch] done"
