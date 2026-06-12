#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:-build/falcon}"
OUTPUT_DIR="${BUILD_DIR}/factory_package"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SDK_TARGET_DIR=""
SDK_ROOT="${SDK_DIR:-}"
SDK_ROOT="${SDK_ROOT%/}"
BUILD_SDK="${FACTORY_BUILD_SDK:-0}"
SDK_DRY_RUN="${FACTORY_SDK_DRY_RUN:-0}"
COPY_SDK_FIRMWARE="${FACTORY_COPY_SDK_FIRMWARE:-0}"
QCA_BT_FW_MODULE="${FACTORY_QCA_BT_FW_MODULE:-FC64EABMD}"
FACTORY_REMOVE_INIT_SCRIPTS="
S90dragonfly
S50usbdevice
S50usbdevice.sh
S40bluetoothd
S36wifibt-init.sh
S51otaupdate
xbotgo_app_monitor.sh
S99-auto-reboot
S95watchdog
S95watchdog.sh
S50nginx
S50fcgiwrap
S60security
"

if [ -n "${FALCON_SDK:-}" ]; then
    SDK_TARGET_DIR="$(dirname "$FALCON_SDK")/target"
    if [ -z "$SDK_ROOT" ]; then
        case "$FALCON_SDK" in
            */buildroot/output/*/host)
                SDK_ROOT="${FALCON_SDK%/buildroot/output/*/host}"
                ;;
        esac
    fi
fi

log()
{
    echo "[build_factory] $*"
}

copy_firmware_tree()
{
    local src="$1"

    if [ -d "$src" ]; then
        log "Copy firmware tree: $src"
        cp -a "$src/." "$OUTPUT_DIR/lib/firmware/"
    fi
}

find_qca_bt_fw_dir()
{
    local legacy_root
    local candidate

    if [ -n "${FACTORY_QCA_BT_FW_DIR:-}" ]; then
        if [ -f "$FACTORY_QCA_BT_FW_DIR/hpbtfw21.tlv" ]; then
            echo "$FACTORY_QCA_BT_FW_DIR"
            return 0
        fi
        log "FACTORY_QCA_BT_FW_DIR missing hpbtfw21.tlv: $FACTORY_QCA_BT_FW_DIR" >&2
    fi

    legacy_root="${FACTORY_LEGACY_FALCON_ROOT:-/home/gdh/falcon/app/lastcode20251210/XbotGo-Dragonfly-Embedded}"
    for candidate in \
        "$FW_ROOT/platforms/falcon/firmware/qca/$QCA_BT_FW_MODULE" \
        "$FW_ROOT/platforms/falcon/firmware/qca" \
        "$legacy_root/btwifi/drivers/BT/FW/$QCA_BT_FW_MODULE"; do
        if [ -f "$candidate/hpbtfw21.tlv" ]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

copy_qca_bt_firmware()
{
    local src
    local file

    src="$(find_qca_bt_fw_dir || true)"
    if [ -z "$src" ]; then
        log "QCA BT firmware not found; set FACTORY_QCA_BT_FW_DIR if this board uses hciattach qca"
        return 0
    fi

    mkdir -p "$OUTPUT_DIR/lib/firmware/qca"
    for file in hpbtfw21.tlv hpnv21.bin hpnv21g.bin hpnv21.nvm; do
        if [ -f "$src/$file" ]; then
            cp -f "$src/$file" "$OUTPUT_DIR/lib/firmware/qca/"
        fi
    done
    log "Copied QCA BT firmware from $src"
}

run_cmd()
{
    if [ "$SDK_DRY_RUN" = "1" ]; then
        printf '[build_factory] DRY-RUN'
        printf ' %q' "$@"
        printf '\n'
        return 0
    fi
    "$@"
}

assert_sdk_subpath()
{
    local path="$1"
    local sdk_abs
    local path_abs

    sdk_abs="$(readlink -m "$SDK_ROOT")"
    path_abs="$(readlink -m "$path")"
    case "$path_abs" in
        "$sdk_abs"/*)
            return 0
            ;;
        *)
            echo "ERROR: refusing to modify path outside SDK_DIR: $path_abs" >&2
            exit 1
            ;;
    esac
}

copy_runtime_to_oem()
{
    local dst="$1"

    log "Updating SDK OEM factory runtime: $dst"
    assert_sdk_subpath "$dst"
    run_cmd rm -rf "$dst"
    run_cmd mkdir -p "$dst"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN copy runtime package to $dst"
    else
        cp -a "$OUTPUT_DIR/." "$dst/"
        chmod -R u=rwX,go=rX "$dst"
    fi
}

write_userdata_flags()
{
    local userdata_dir="$1"

    run_cmd mkdir -p "$userdata_dir"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN write factory userdata flags to $userdata_dir"
    else
        : > "$userdata_dir/factory_mode"
        : > "$userdata_dir/aging_time.conf"
    fi
}

remove_conflicting_init_scripts()
{
    local init_dir="$1"
    local script

    for script in $FACTORY_REMOVE_INIT_SCRIPTS; do
        run_cmd rm -f "$init_dir/$script"
    done
}

install_init_script()
{
    local rootfs_dir="$1"

    log "Installing factory init script into rootfs: $rootfs_dir"
    run_cmd mkdir -p "$rootfs_dir/etc/init.d"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN install S90factory_fw to $rootfs_dir/etc/init.d"
    else
        cp -f "$SCRIPT_DIR/init.d/S90factory_fw" "$rootfs_dir/etc/init.d/"
        chmod 755 "$rootfs_dir/etc/init.d/S90factory_fw"
    fi
    remove_conflicting_init_scripts "$rootfs_dir/etc/init.d"
}

install_adb_auth_files()
{
    local rootfs_dir="$1"

    log "Installing factory ADB auth files into rootfs: $rootfs_dir"
    if [ -f "$OUTPUT_DIR/adb_keys" ]; then
        if [ "$SDK_DRY_RUN" = "1" ]; then
            log "DRY-RUN install adb_keys to $rootfs_dir/adb_keys"
        else
            cp -f "$OUTPUT_DIR/adb_keys" "$rootfs_dir/adb_keys"
            chmod 644 "$rootfs_dir/adb_keys"
        fi
    fi

    if [ -f "$OUTPUT_DIR/etc/profile.d/adbd.sh" ]; then
        run_cmd mkdir -p "$rootfs_dir/etc/profile.d"
        if [ "$SDK_DRY_RUN" = "1" ]; then
            log "DRY-RUN install adbd.sh to $rootfs_dir/etc/profile.d/adbd.sh"
        else
            cp -f "$OUTPUT_DIR/etc/profile.d/adbd.sh" "$rootfs_dir/etc/profile.d/adbd.sh"
            chmod 644 "$rootfs_dir/etc/profile.d/adbd.sh"
        fi
    fi
}

install_rootfs_bt_firmware()
{
    local rootfs_dir="$1"
    local src="$OUTPUT_DIR/lib/firmware/qca"
    local dst="$rootfs_dir/usr/lib/firmware/qca"

    [ -d "$src" ] || return 0

    log "Installing QCA BT firmware into rootfs: $dst"
    run_cmd mkdir -p "$dst"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN copy QCA BT firmware to $dst"
    else
        cp -a "$src/." "$dst/"
        chmod -R u=rwX,go=rX "$dst"
    fi
}

install_rootfs_iqfiles()
{
    local rootfs_dir="$1"
    local src="$OUTPUT_DIR/iqfiles"
    local dst="$rootfs_dir/etc/iqfiles"

    [ -d "$src" ] || return 0

    log "Installing Falcon camera IQ files into rootfs: $dst"
    run_cmd mkdir -p "$dst"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN copy Falcon IQ files to $dst"
    else
        cp -a "$src/." "$dst/"
        chmod -R u=rwX,go=rX "$dst"
    fi
}

install_rootfs_overlay()
{
    local overlay_dir="$1"

    log "Installing factory rootfs overlay: $overlay_dir"
    run_cmd mkdir -p "$overlay_dir/etc/init.d"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN install Buildroot overlay for S90factory_fw"
    else
        cp -f "$SCRIPT_DIR/init.d/S90factory_fw" "$overlay_dir/etc/init.d/"
        chmod 755 "$overlay_dir/etc/init.d/S90factory_fw"
        if [ -f "$OUTPUT_DIR/adb_keys" ]; then
            cp -f "$OUTPUT_DIR/adb_keys" "$overlay_dir/adb_keys"
            chmod 644 "$overlay_dir/adb_keys"
        fi
        if [ -f "$OUTPUT_DIR/etc/profile.d/adbd.sh" ]; then
            mkdir -p "$overlay_dir/etc/profile.d"
            cp -f "$OUTPUT_DIR/etc/profile.d/adbd.sh" "$overlay_dir/etc/profile.d/adbd.sh"
            chmod 644 "$overlay_dir/etc/profile.d/adbd.sh"
        fi
        if [ -d "$OUTPUT_DIR/lib/firmware/qca" ]; then
            mkdir -p "$overlay_dir/usr/lib/firmware/qca"
            cp -a "$OUTPUT_DIR/lib/firmware/qca/." "$overlay_dir/usr/lib/firmware/qca/"
            chmod -R u=rwX,go=rX "$overlay_dir/usr/lib/firmware/qca"
        fi
        if [ -d "$OUTPUT_DIR/iqfiles" ]; then
            mkdir -p "$overlay_dir/etc/iqfiles"
            cp -a "$OUTPUT_DIR/iqfiles/." "$overlay_dir/etc/iqfiles/"
            chmod -R u=rwX,go=rX "$overlay_dir/etc/iqfiles"
        fi
        : > "$overlay_dir/.skip_fsck"
        cat > "$overlay_dir/prepare.sh" <<'EOF'
#!/bin/sh
set -u

TARGET_DIR="${1:-}"
[ -n "$TARGET_DIR" ] || exit 0

rm -f "$TARGET_DIR/etc/init.d/S90dragonfly" \
      "$TARGET_DIR/etc/init.d/S50usbdevice" \
      "$TARGET_DIR/etc/init.d/S50usbdevice.sh" \
      "$TARGET_DIR/etc/init.d/S40bluetoothd" \
      "$TARGET_DIR/etc/init.d/S36wifibt-init.sh" \
      "$TARGET_DIR/etc/init.d/S51otaupdate" \
      "$TARGET_DIR/etc/init.d/xbotgo_app_monitor.sh" \
      "$TARGET_DIR/etc/init.d/S99-auto-reboot" \
      "$TARGET_DIR/etc/init.d/S95watchdog" \
      "$TARGET_DIR/etc/init.d/S95watchdog.sh" \
      "$TARGET_DIR/etc/init.d/S50nginx" \
      "$TARGET_DIR/etc/init.d/S50fcgiwrap" \
      "$TARGET_DIR/etc/init.d/S60security"

exit 0
EOF
        chmod 755 "$overlay_dir/prepare.sh"
    fi
}

ensure_skip_fsck()
{
    local rootfs_dir="$1"

    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN touch $rootfs_dir/.skip_fsck"
    else
        touch "$rootfs_dir/.skip_fsck"
    fi
}

apply_sdk_patch()
{
    if [ "${FACTORY_APPLY_SDK_PATCH:-1}" = "0" ]; then
        log "SDK patch disabled"
        return 0
    fi

    local patch_dir="${FACTORY_SOURCE_PATCH_DIR:-$SCRIPT_DIR/sdk_patch}"
    if [ "$SDK_DRY_RUN" = "1" ]; then
        FACTORY_PATCH_DRY_RUN=1 sh "$SCRIPT_DIR/sdk_patch/apply_factory_patch.sh" "$SDK_ROOT" "$patch_dir"
    else
        sh "$SCRIPT_DIR/sdk_patch/apply_factory_patch.sh" "$SDK_ROOT" "$patch_dir"
    fi
}

copy_lvgl_resources()
{
    local dst="$OUTPUT_DIR/conf/lvgl_source"
    local src=""
    local candidate

    for candidate in \
        "${FACTORY_LVGL_SOURCE_DIR:-}" \
        "$SCRIPT_DIR/resources/lvgl_source"; do
        [ -n "$candidate" ] || continue
        if [ -d "$candidate" ] && [ -f "$candidate/Rajdhani/Rajdhani-SemiBold-5.ttf" ]; then
            src="$candidate"
            break
        fi
    done

    if [ -z "$src" ]; then
        echo "ERROR: LVGL resources not found; set FACTORY_LVGL_SOURCE_DIR to the lvgl_source directory" >&2
        exit 1
    fi

    log "Copying LVGL resources: $src"
    rm -rf "$dst"
    mkdir -p "$dst"
    cp -a "$src/." "$dst/"
}

build_sdk()
{
    if [ "$SDK_DRY_RUN" = "1" ]; then
        log "DRY-RUN run SDK build in $SDK_ROOT"
        return 0
    fi

    log "Running SDK firmware build: $SDK_ROOT/build.sh"
    (cd "$SDK_ROOT" && ./build.sh)
}

integrate_sdk()
{
    if [ -z "$SDK_ROOT" ]; then
        if [ "$BUILD_SDK" = "1" ] || [ "$SDK_DRY_RUN" = "1" ]; then
            echo "ERROR: SDK_DIR is required for firmware integration." >&2
            echo "       Example:" >&2
            echo "       export SDK_DIR=/home/gdh/falcon/Omni3576-sdk" >&2
            echo '       export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host' >&2
            echo "       FACTORY_BUILD_SDK=1 PLATFORM=falcon ./build.sh" >&2
            exit 1
        fi
        log "SDK_DIR not set, skip SDK firmware integration"
        return 0
    fi

    local oem_usr_dir="$SDK_ROOT/device/rockchip/common/extra-parts/oem/normal/usr"
    local userdata_dir="$SDK_ROOT/device/rockchip/common/extra-parts/userdata/testdata"
    local overlay_dir="$SDK_ROOT/buildroot/board/rockchip/common/overlays/factory_fw"
    local rootfs_dir="${FACTORY_SDK_ROOTFS_DIR:-$SDK_ROOT/buildroot/output/rockchip_rk3576_ipc/target}"

    if [ ! -d "$SDK_ROOT" ]; then
        echo "ERROR: SDK_DIR does not exist: $SDK_ROOT" >&2
        exit 1
    fi

    log "Integrating factory runtime into SDK: $SDK_ROOT"
    log "OEM usr: $oem_usr_dir"
    log "userdata: $userdata_dir"
    log "rootfs: $rootfs_dir"

    copy_runtime_to_oem "$oem_usr_dir"
    write_userdata_flags "$userdata_dir"

    apply_sdk_patch
    install_rootfs_overlay "$overlay_dir"

    if [ -d "$rootfs_dir" ]; then
        install_init_script "$rootfs_dir"
        install_adb_auth_files "$rootfs_dir"
        install_rootfs_bt_firmware "$rootfs_dir"
        install_rootfs_iqfiles "$rootfs_dir"
    else
        log "rootfs target not found yet: $rootfs_dir"
    fi

    if [ "$BUILD_SDK" = "1" ]; then
        build_sdk
        if [ -d "$rootfs_dir" ]; then
            install_init_script "$rootfs_dir"
            install_adb_auth_files "$rootfs_dir"
            install_rootfs_bt_firmware "$rootfs_dir"
            install_rootfs_iqfiles "$rootfs_dir"
            ensure_skip_fsck "$rootfs_dir"
        fi
    else
        log "SDK build skipped; set FACTORY_BUILD_SDK=1 to run SDK ./build.sh"
    fi
}

log "Packaging runtime from $BUILD_DIR"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/bin" \
         "$OUTPUT_DIR/conf" \
         "$OUTPUT_DIR/etc/dbus-1" \
         "$OUTPUT_DIR/scripts" \
         "$OUTPUT_DIR/init.d" \
         "$OUTPUT_DIR/lib" \
         "$OUTPUT_DIR/lib/firmware" \
         "$OUTPUT_DIR/lib/modules" \
         "$OUTPUT_DIR/libexec/bluetooth" \
         "$OUTPUT_DIR/iqfiles" \
         "$OUTPUT_DIR/sdk_patch"

cp -f "$BUILD_DIR/factory_test" "$OUTPUT_DIR/bin/"

cp -f "$SCRIPT_DIR/platform.json" "$OUTPUT_DIR/conf/"
cp -f "$SCRIPT_DIR/tests.json" "$OUTPUT_DIR/conf/"
cp -f "$SCRIPT_DIR/conf/"* "$OUTPUT_DIR/conf/"
if [ -d "$SCRIPT_DIR/iqfiles" ]; then
    cp -a "$SCRIPT_DIR/iqfiles/." "$OUTPUT_DIR/iqfiles/"
fi
copy_lvgl_resources

cp -f "$SCRIPT_DIR/scripts/"* "$OUTPUT_DIR/scripts/"
cp -f "$SCRIPT_DIR/init.d/"* "$OUTPUT_DIR/init.d/"
cp -a "$SCRIPT_DIR/sdk_patch/." "$OUTPUT_DIR/sdk_patch/"
chmod 755 "$OUTPUT_DIR/scripts/"*.sh "$OUTPUT_DIR/init.d/"* "$OUTPUT_DIR/sdk_patch/"*.sh

for tool in mosquitto dbus-daemon dbus-uuidgen adbd arecord amixer brcm_patchram_plus1 btattach hciattach hciconfig rk_hciattach rtk_hciattach wifibt-init.sh wifibt-util.sh bt-tty wifibt-bus wifibt-chip wifibt-id wifibt-info wifibt-module wifibt-vendor rkaiq_3A_server; do
    if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/usr/bin/$tool" ]; then
        cp -P "$SDK_TARGET_DIR/usr/bin/$tool" "$OUTPUT_DIR/bin/"
    fi
done

if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/adb_keys" ]; then
    cp -f "$SDK_TARGET_DIR/adb_keys" "$OUTPUT_DIR/"
fi
if [ -n "$SDK_TARGET_DIR" ] && [ -f "$SDK_TARGET_DIR/etc/profile.d/adbd.sh" ]; then
    mkdir -p "$OUTPUT_DIR/etc/profile.d"
    cp -f "$SDK_TARGET_DIR/etc/profile.d/adbd.sh" "$OUTPUT_DIR/etc/profile.d/"
fi

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

if [ -n "$SDK_TARGET_DIR" ] && [ -d "$SDK_TARGET_DIR/usr/lib/modules" ]; then
    find "$SDK_TARGET_DIR/usr/lib/modules" -maxdepth 1 -type f -name '*.ko' \
        -exec cp -f {} "$OUTPUT_DIR/lib/modules/" \;
fi

if [ "$COPY_SDK_FIRMWARE" = "1" ] && [ -n "$SDK_TARGET_DIR" ]; then
    copy_firmware_tree "$SDK_TARGET_DIR/lib/firmware"
    copy_firmware_tree "$SDK_TARGET_DIR/usr/lib/firmware"
else
    log "Skip SDK firmware tree copy; set FACTORY_COPY_SDK_FIRMWARE=1 to include it"
fi
copy_qca_bt_firmware

MOTOR_KO="${FACTORY_MOTOR_KO:-$SCRIPT_DIR/sdk_patch/kernel_patch/motor_tmi8152/motor_tmi8152.ko}"
if [ -f "$MOTOR_KO" ]; then
    cp -f "$MOTOR_KO" "$OUTPUT_DIR/conf/"
fi

BATTERY_KO="${FACTORY_BATTERY_KO:-$SCRIPT_DIR/sdk_patch/kernel_patch/om70x0x_battery/battery.ko}"
if [ -f "$BATTERY_KO" ]; then
    mkdir -p "$OUTPUT_DIR/lib/modules"
    cp -f "$BATTERY_KO" "$OUTPUT_DIR/lib/modules/battery.ko"
fi

tar czf "${BUILD_DIR}/factory_firmware.tar.gz" -C "$OUTPUT_DIR" .

log "Runtime package done: ${BUILD_DIR}/factory_firmware.tar.gz"
integrate_sdk
