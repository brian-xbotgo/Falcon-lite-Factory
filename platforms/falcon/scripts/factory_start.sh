#!/bin/sh
set -u

OEM_DIR="${FACTORY_OEM_DIR:-/oem}"
USR_DIR="$OEM_DIR/usr"
BIN_DIR="$USR_DIR/bin"
CONF_DIR="$USR_DIR/conf"
SCRIPT_DIR="$USR_DIR/scripts"
LIB_DIR="$USR_DIR/lib"
IQ_DIR="$USR_DIR/iqfiles"
LOG_DIR="${FACTORY_LOG_DIR:-/userdata/logs}"
RUN_DIR=/var/run/factory_fw
FACTORY_FLAG=/userdata/factory_mode
RKAIQ_IQ_NAMES="imx678_OT01_40IRC_F16.json gc4663_CMK-OT2022-PX1_IR0147-50IRC-8M-F20.json"

export PATH="$BIN_DIR:/usr/bin:/usr/sbin:/bin:/sbin:$PATH"
export LD_LIBRARY_PATH="$LIB_DIR:/usr/lib:/lib:${LD_LIBRARY_PATH:-}"
export FACTORY_PLATFORM_JSON="${FACTORY_PLATFORM_JSON:-$CONF_DIR/platform.json}"
export FACTORY_TESTS_JSON="${FACTORY_TESTS_JSON:-$CONF_DIR/tests.json}"
export WIFIBT_MODULE_DIR="${WIFIBT_MODULE_DIR:-$LIB_DIR/modules}"
export WIFIBT_FIRMWARE_DIR="${WIFIBT_FIRMWARE_DIR:-$LIB_DIR/firmware}"

log()
{
    echo "[factory_start] $*"
}

ensure_dirs()
{
    mkdir -p "$LOG_DIR" "$RUN_DIR" /userdata/prod /userdata/record /device_data /tmp /var/tmp
    touch /userdata/logs/prod_test_new.log 2>/dev/null || true
    if mount | grep -q " on /device_data "; then
        mount -o remount,rw /device_data 2>/dev/null || log "remount /device_data rw failed"
    fi
    touch "$FACTORY_FLAG" 2>/dev/null || true
}

kill_by_pidfile()
{
    pidfile="$1"
    if [ -f "$pidfile" ]; then
        pid="$(cat "$pidfile" 2>/dev/null || true)"
        if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
        rm -f "$pidfile"
    fi
}

stop_conflicting_processes()
{
    local name
    local pattern
    local pids

    for pattern in \
        xbotgo_app_monitor.sh \
        S99-auto-reboot \
        S51otaupdate; do
        pids="$(ps 2>/dev/null | awk -v pat="$pattern" '$0 ~ pat && $0 !~ /awk/ {print $1}')"
        [ -n "$pids" ] || continue
        log "stop conflicting script: $pattern"
        kill $pids >/dev/null 2>&1 || true
    done

    for name in \
        xbotgo_app_monitor.sh \
        ota_update \
        updateEngine \
        misc_app \
        prod_test \
        normal_lvgl_app \
        charge_lvgl_app \
        multi_media \
        file_mng \
        http_agent \
        nginx \
        fcgiwrap \
        rkipc; do
        if pidof "$name" >/dev/null 2>&1; then
            log "stop conflicting process: $name"
            killall "$name" >/dev/null 2>&1 || true
        fi
    done
}

start_mqtt()
{
    if pidof mosquitto >/dev/null 2>&1; then
        log "mosquitto already running"
        return 0
    fi

    if [ -x "$BIN_DIR/mosquitto" ]; then
        broker="$BIN_DIR/mosquitto"
    elif command -v mosquitto >/dev/null 2>&1; then
        broker="$(command -v mosquitto)"
    else
        log "mosquitto not found"
        return 1
    fi

    conf="$CONF_DIR/mosquitto.conf"
    [ -f "$conf" ] || conf=/etc/mosquitto/mosquitto.conf
    log "start mosquitto conf=$conf"
    "$broker" -c "$conf" > "$LOG_DIR/mosquitto.log" 2>&1 < /dev/null &
    echo $! > "$RUN_DIR/mosquitto.pid"
    sleep 1
}

start_rndis()
{
    if command -v usbdevice >/dev/null 2>&1; then
        usbdevice stop >> "$LOG_DIR/factory_rndis.log" 2>&1 || true
    fi

    if [ "${FACTORY_ENABLE_RNDIS:-1}" = "0" ]; then
        log "RNDIS disabled, start USB as ADB-only"
        FACTORY_USB_MODE=adb "$SCRIPT_DIR/factory_rndis.sh" start >> "$LOG_DIR/factory_rndis.log" 2>&1 || log "ADB-only USB init failed"
        return 0
    fi
    if [ -x "$SCRIPT_DIR/factory_rndis.sh" ]; then
        log "start USB mode=${FACTORY_USB_MODE:-auto}"
        "$SCRIPT_DIR/factory_rndis.sh" start >> "$LOG_DIR/factory_rndis.log" 2>&1 || log "RNDIS init failed"
    else
        log "factory_rndis.sh missing"
    fi
}

load_modules()
{
    if [ "${FACTORY_LOAD_MODULES:-1}" = "0" ]; then
        log "module loading disabled"
        return 0
    fi

    for module in "$LIB_DIR/modules/battery.ko" "$CONF_DIR/motor_tmi8152.ko" "$LIB_DIR/modules/motor_tmi8152.ko"; do
        [ -f "$module" ] || continue
        name="$(basename "$module" .ko)"
        if lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$name"; then
            continue
        fi
        insmod "$module" >> "$LOG_DIR/modules.log" 2>&1 || log "insmod failed: $module"
    done
}

hci0_is_up()
{
    hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"
}

hci0_exists()
{
    hciconfig hci0 >/dev/null 2>&1
}

bring_hci0_up()
{
    if hci0_is_up; then
        return 0
    fi
    if ! hci0_exists; then
        return 1
    fi

    hciconfig hci0 up >> "$LOG_DIR/bluetooth.log" 2>&1 || return 1
    hci0_is_up
}

wait_for_hci0()
{
    timeout="${1:-10}"
    while [ "$timeout" -gt 0 ]; do
        if bring_hci0_up; then
            return 0
        fi
        sleep 1
        timeout=$((timeout - 1))
    done
    return 1
}

stop_hci_attach()
{
    kill_by_pidfile "$RUN_DIR/hciattach.pid"
    killall hciattach >/dev/null 2>&1 || true
    killall brcm_patchram_plus1 >/dev/null 2>&1 || true
    killall rk_hciattach >/dev/null 2>&1 || true
    killall rtk_hciattach >/dev/null 2>&1 || true
}

bt_rfkill_states()
{
    for type_file in /sys/class/rfkill/rfkill*/type; do
        [ -f "$type_file" ] || continue
        if grep -qi "^bluetooth$" "$type_file" 2>/dev/null; then
            echo "${type_file%/type}/state"
        fi
    done
}

reset_bt_rfkill()
{
    states="$(bt_rfkill_states)"
    [ -n "$states" ] || return 0

    for state in $states; do
        [ -w "$state" ] && echo 0 > "$state" 2>/dev/null || true
    done
    [ -w /proc/bluetooth/sleep/btwrite ] && echo 0 > /proc/bluetooth/sleep/btwrite 2>/dev/null || true
    sleep 1
    for state in $states; do
        [ -w "$state" ] && echo 1 > "$state" 2>/dev/null || true
    done
    [ -w /proc/bluetooth/sleep/btwrite ] && echo 1 > /proc/bluetooth/sleep/btwrite 2>/dev/null || true
    sleep 1
}

root_is_readonly()
{
    mount | awk '$3 == "/" && $6 ~ /(^|,)ro(,|$)/ { found = 1 } END { exit !found }'
}

prepare_firmware_target_dir()
{
    target="$1"

    if [ -d "$target" ]; then
        return 0
    fi
    if mkdir -p "$target" 2>/dev/null; then
        return 0
    fi
    if mount -o remount,rw / 2>/dev/null && mkdir -p "$target" 2>/dev/null; then
        return 0
    fi

    log "cannot create firmware target dir: $target"
    return 1
}

expose_bluetooth_firmware()
{
    src_qca="$WIFIBT_FIRMWARE_DIR/qca"
    dst_qca=/lib/firmware/qca
    root_was_ro=0

    [ -d "$src_qca" ] || return 0
    if [ -f "$dst_qca/hpbtfw21.tlv" ]; then
        return 0
    fi

    if root_is_readonly; then
        root_was_ro=1
    fi

    if ! prepare_firmware_target_dir "$dst_qca"; then
        if [ "$root_was_ro" = "1" ]; then
            mount -o remount,ro / 2>/dev/null || true
        fi
        return 1
    fi

    if mount | awk -v target="$dst_qca" '$3 == target { found = 1 } END { exit !found }'; then
        :
    elif mount -o bind "$src_qca" "$dst_qca" 2>/dev/null; then
        log "bind BT QCA firmware: $src_qca -> $dst_qca"
    else
        log "bind BT QCA firmware failed; creating symlinks in $dst_qca"
        for fw in "$src_qca"/*; do
            [ -f "$fw" ] || continue
            ln -sf "$fw" "$dst_qca/$(basename "$fw")" 2>/dev/null || true
        done
    fi

    if [ "$root_was_ro" = "1" ]; then
        mount -o remount,ro / 2>/dev/null || true
    fi
}

bt_tty()
{
    if [ -n "${FACTORY_BT_TTY:-}" ]; then
        echo "$FACTORY_BT_TTY"
        return 0
    fi

    if command -v bt-tty >/dev/null 2>&1; then
        tty="$(bt-tty 2>/dev/null || true)"
        if [ -n "$tty" ] && [ "$tty" != "unknown" ] && [ "$tty" != "none" ]; then
            echo "$tty"
            return 0
        fi
    fi

    for tty in /dev/ttyS4 /dev/ttyS2; do
        if [ -e "$tty" ]; then
            echo "$tty"
            return 0
        fi
    done
    return 1
}

start_wifibt_stack()
{
    if ! command -v wifibt-init.sh >/dev/null 2>&1; then
        return 1
    fi

    expose_bluetooth_firmware || true
    log "start Wi-Fi/BT stack for Bluetooth module_dir=$WIFIBT_MODULE_DIR firmware_dir=$WIFIBT_FIRMWARE_DIR"
    {
        command -v wifibt-info >/dev/null 2>&1 && wifibt-info || true
        wifibt-init.sh start
    } >> "$LOG_DIR/bluetooth.log" 2>&1 || log "wifibt-init start failed"
}

start_broadcom_fallback()
{
    tty="$1"

    if ! command -v brcm_patchram_plus1 >/dev/null 2>&1; then
        return 1
    fi

    killall brcm_patchram_plus1 >/dev/null 2>&1 || true
    reset_bt_rfkill
    log "start BT Broadcom attach tty=$tty firmware_dir=$WIFIBT_FIRMWARE_DIR"
    brcm_patchram_plus1 --enable_hci --no2bytes \
        --use_baudrate_for_download --tosleep 200000 \
        --baudrate "${FACTORY_BT_BAUD:-1500000}" \
        --patchram "$WIFIBT_FIRMWARE_DIR/" "$tty" >> "$LOG_DIR/bluetooth.log" 2>&1 < /dev/null &
    echo $! > "$RUN_DIR/hciattach.pid"
}

start_rockchip_fallback()
{
    tty="$1"

    if command -v rk_hciattach >/dev/null 2>&1; then
        attach=rk_hciattach
        killall rk_hciattach >/dev/null 2>&1 || true
        reset_bt_rfkill
        log "start BT Rockchip attach tty=$tty"
        "$attach" -n -s 115200 "$tty" rockchip 3000000 flow nosleep 11:22:33:44:55:66 >> "$LOG_DIR/bluetooth.log" 2>&1 < /dev/null &
        echo $! > "$RUN_DIR/hciattach.pid"
        return 0
    fi

    return 1
}

start_realtek_fallback()
{
    tty="$1"

    if command -v rtk_hciattach >/dev/null 2>&1; then
        try_insmod_module hci_uart 1
        killall rtk_hciattach >/dev/null 2>&1 || true
        reset_bt_rfkill
        log "start BT Realtek attach tty=$tty"
        rtk_hciattach -n -s 115200 "$tty" rtk_h5 >> "$LOG_DIR/bluetooth.log" 2>&1 < /dev/null &
        echo $! > "$RUN_DIR/hciattach.pid"
        return 0
    fi

    return 1
}

try_insmod_module()
{
    module="$1"
    delay="${2:-0}"
    if lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$module"; then
        return 0
    fi
    if [ -f "$WIFIBT_MODULE_DIR/$module.ko" ]; then
        log "insmod BT module $module"
        insmod "$WIFIBT_MODULE_DIR/$module.ko" >> "$LOG_DIR/bluetooth.log" 2>&1 || return 1
        sleep "$delay"
        return 0
    fi
    return 1
}

start_generic_hciattach()
{
    if bring_hci0_up; then
        return 0
    fi

    if hci0_exists; then
        log "hci0 exists but is not usable; restart HCI attach"
    fi

    expose_bluetooth_firmware || true
    tty="$(bt_tty || true)"
    if [ -z "$tty" ]; then
        log "BT tty not found"
        return 1
    fi

    attach="${FACTORY_BT_ATTACH:-hciattach}"
    proto="${FACTORY_BT_PROTO:-qca}"
    baud="${FACTORY_BT_BAUD:-3000000}"
    stop_hci_attach

    for attempt in 1 2; do
        reset_bt_rfkill
        log "start BT HCI attach attempt=$attempt tty=$tty proto=$proto baud=$baud"
        "$attach" "$tty" "$proto" "$baud" flow >> "$LOG_DIR/bluetooth.log" 2>&1 < /dev/null &
        echo $! > "$RUN_DIR/hciattach.pid"

        if wait_for_hci0 10; then
            return 0
        fi

        kill_by_pidfile "$RUN_DIR/hciattach.pid"
        killall "$attach" >/dev/null 2>&1 || true
        sleep 1
    done

    return 1
}

start_hciattach_fallback()
{
    if bring_hci0_up; then
        return 0
    fi

    if hci0_exists; then
        log "restart stale hci0 before BT fallback"
        stop_hci_attach
    fi

    tty="$(bt_tty || true)"
    if [ -z "$tty" ]; then
        log "BT tty not found"
        return 1
    fi

    vendor=""
    if command -v wifibt-vendor >/dev/null 2>&1; then
        vendor="$(wifibt-vendor 2>/dev/null || true)"
    fi
    log "BT fallback vendor=${vendor:-unknown} tty=$tty"

    case "$vendor" in
        Broadcom)
            start_broadcom_fallback "$tty" || true
            ;;
        Rockchip)
            start_rockchip_fallback "$tty" || true
            ;;
        Realtek)
            start_realtek_fallback "$tty" || true
            ;;
    esac

    if wait_for_hci0 10; then
        return 0
    fi

    start_generic_hciattach
}

find_bluetoothd()
{
    for bt in \
        "$USR_DIR/libexec/bluetooth/bluetoothd" \
        /usr/libexec/bluetooth/bluetoothd \
        /usr/lib/bluetooth/bluetoothd; do
        if [ -x "$bt" ]; then
            echo "$bt"
            return 0
        fi
    done

    command -v bluetoothd 2>/dev/null || true
}

stop_bluetoothd()
{
    kill_by_pidfile "$RUN_DIR/bluetoothd.pid"
    if pidof bluetoothd >/dev/null 2>&1; then
        log "restart bluetoothd with experimental mode"
        killall bluetoothd >/dev/null 2>&1 || true
        sleep 1
    fi
}

start_bluetoothd()
{
    bt="$(find_bluetoothd)"
    if [ -z "$bt" ]; then
        log "bluetoothd not found"
        return 1
    fi

    args="-E -n"
    if [ "${FACTORY_BLUETOOTHD_DEBUG:-0}" = "1" ]; then
        args="$args -d"
    fi

    log "start bluetoothd args=$args"
    # shellcheck disable=SC2086
    if command -v setsid >/dev/null 2>&1; then
        setsid "$bt" $args >> "$LOG_DIR/bluetoothd.log" 2>&1 < /dev/null &
    elif command -v nohup >/dev/null 2>&1; then
        nohup "$bt" $args >> "$LOG_DIR/bluetoothd.log" 2>&1 < /dev/null &
    else
        (trap '' HUP; "$bt" $args >> "$LOG_DIR/bluetoothd.log" 2>&1 < /dev/null) &
    fi
    echo $! > "$RUN_DIR/bluetoothd.pid"
    sleep 1
    pidof bluetoothd >/dev/null 2>&1 || log "bluetoothd exited after startup"
}

bluez_adapter_has_managers()
{
    if command -v gdbus >/dev/null 2>&1; then
        gdbus introspect --system --dest org.bluez --object-path /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.Adapter1" &&
        gdbus introspect --system --dest org.bluez --object-path /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.GattManager1" &&
        gdbus introspect --system --dest org.bluez --object-path /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.LEAdvertisingManager1"
        return $?
    fi

    if command -v busctl >/dev/null 2>&1; then
        busctl --system introspect org.bluez /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.Adapter1" &&
        busctl --system introspect org.bluez /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.GattManager1" &&
        busctl --system introspect org.bluez /org/bluez/hci0 2>/dev/null |
            grep -q "org.bluez.LEAdvertisingManager1"
        return $?
    fi

    return 2
}

wait_bluez_adapter_managers()
{
    timeout="${1:-20}"
    while [ "$timeout" -gt 0 ]; do
        bluez_adapter_has_managers
        rc=$?
        if [ "$rc" -eq 0 ]; then
            log "BlueZ hci0 Adapter/GATT/Advertising managers ready"
            return 0
        fi
        if [ "$rc" -eq 2 ]; then
            log "skip BlueZ manager precheck: gdbus/busctl not found"
            return 0
        fi
        sleep 1
        timeout=$((timeout - 1))
    done

    log "BlueZ hci0 managers not ready yet; factory_test will keep waiting"
    return 1
}

init_bluetooth()
{
    if [ "${FACTORY_ENABLE_BLE:-1}" = "0" ]; then
        log "BLE disabled"
        return 0
    fi

    if ! bring_hci0_up; then
        if hci0_exists; then
            log "hci0 exists but cannot be brought up; restart HCI attach"
            stop_bluetoothd
            stop_hci_attach
        fi

        reset_bt_rfkill
        expose_bluetooth_firmware || true
        start_wifibt_stack || true
        if ! wait_for_hci0 20; then
            start_hciattach_fallback || true
        fi
    fi

    if ! wait_for_hci0 5; then
        log "hci0 not found, skip bluetoothd startup"
        return 0
    fi

    if command -v hciconfig >/dev/null 2>&1; then
        hciconfig hci0 >> "$LOG_DIR/bluetooth.log" 2>&1 || true
    fi

    stop_bluetoothd
    start_bluetoothd || return 0
    wait_bluez_adapter_managers 20 || true
}

start_dbus()
{
    if pidof dbus-daemon >/dev/null 2>&1; then
        return 0
    fi

    if [ -x "$BIN_DIR/dbus-uuidgen" ]; then
        "$BIN_DIR/dbus-uuidgen" --ensure >/dev/null 2>&1 || true
    elif command -v dbus-uuidgen >/dev/null 2>&1; then
        dbus-uuidgen --ensure >/dev/null 2>&1 || true
    fi

    mkdir -p /run/dbus /var/lock/subsys /tmp/dbus
    if [ -x "$BIN_DIR/dbus-daemon" ]; then
        "$BIN_DIR/dbus-daemon" --system >> "$LOG_DIR/dbus.log" 2>&1 || log "dbus-daemon start failed"
    elif command -v dbus-daemon >/dev/null 2>&1; then
        dbus-daemon --system >> "$LOG_DIR/dbus.log" 2>&1 || log "dbus-daemon start failed"
    else
        log "dbus-daemon not found"
    fi
}

start_nginx()
{
    if pidof nginx >/dev/null 2>&1; then
        return 0
    fi

    conf="$CONF_DIR/nginx_factory.conf"
    [ -f "$conf" ] || conf=/etc/nginx/nginx.conf

    if [ -x "$BIN_DIR/nginx" ]; then
        nginx_bin="$BIN_DIR/nginx"
    elif command -v nginx >/dev/null 2>&1; then
        nginx_bin="$(command -v nginx)"
    else
        log "nginx not found"
        return 0
    fi

    mkdir -p /userdata/prod /userdata/record /var/log/nginx /var/tmp/nginx
    log "start nginx conf=$conf"
    "$nginx_bin" -c "$conf" >> "$LOG_DIR/nginx.log" 2>&1 || log "nginx start failed"
}

cleanup_rkaiq_ipc()
{
    rm -f /tmp/aiq0.lock /tmp/aiq1.lock /tmp/.rkaiq_3A /tmp/rkaiq_* /tmp/*.rkaiq /var/tmp/rkipc 2>/dev/null || true
    ipcrm -a >/dev/null 2>&1 || true
}

log_rkaiq_inputs()
{
    missing=""
    if [ ! -d "$IQ_DIR" ]; then
        missing="$missing $IQ_DIR"
    else
        for name in $RKAIQ_IQ_NAMES; do
            [ -f "$IQ_DIR/$name" ] || missing="$missing $IQ_DIR/$name"
        done
    fi
    if [ -n "$missing" ]; then
        log "rkaiq input warning:$missing"
    fi
}

wait_rkaiq_launch_files()
{
    timeout="${1:-20}"
    while [ "$timeout" -gt 0 ]; do
        if [ -x "$BIN_DIR/rkaiq_3A_server" ] || command -v rkaiq_3A_server >/dev/null 2>&1; then
            if [ -d "$IQ_DIR" ] || [ -d /etc/iqfiles ]; then
                return 0
            fi
        fi
        if [ "$timeout" = "20" ] || [ "$timeout" = "10" ] || [ "$timeout" = "1" ]; then
            log "waiting rkaiq launch files"
        fi
        sleep 1
        timeout=$((timeout - 1))
    done
    return 1
}

start_rkaiq()
{
    wait_rkaiq_launch_files 20 || true
    if [ -x "$BIN_DIR/rkaiq_3A_server" ]; then
        rkaiq_bin="$BIN_DIR/rkaiq_3A_server"
    elif command -v rkaiq_3A_server >/dev/null 2>&1; then
        rkaiq_bin="$(command -v rkaiq_3A_server)"
    else
        log "rkaiq_3A_server not found"
        return 0
    fi

    iq_dir="$IQ_DIR"
    [ -d "$iq_dir" ] || iq_dir=/etc/iqfiles
    log_rkaiq_inputs

    if pidof rkaiq_3A_server >/dev/null 2>&1; then
        log "rkaiq_3A_server already running"
        return 0
    fi

    : > "$LOG_DIR/rkaiq_3A_server.log" 2>/dev/null || true
    for attempt in 1 2 3; do
        cleanup_rkaiq_ipc
        log "start rkaiq_3A_server attempt=$attempt iq_dir=$iq_dir"
        "$rkaiq_bin" -a "$iq_dir" >> "$LOG_DIR/rkaiq_3A_server.log" 2>&1 < /dev/null &
        echo $! > "$RUN_DIR/rkaiq_3A_server.pid"
        sleep 3
        if pidof rkaiq_3A_server >/dev/null 2>&1; then
            log "rkaiq_3A_server running"
            return 0
        fi
        log "rkaiq_3A_server exited attempt=$attempt"
    done
}

prepare_wifi_identity()
{
    cpu_file=/userdata/cpuinfo.txt
    serial=""

    if [ -r /proc/cpuinfo ]; then
        serial="$(awk '/Serial/ {print $NF; exit}' /proc/cpuinfo 2>/dev/null || true)"
    fi
    if [ -n "$serial" ]; then
        if [ ! -f "$cpu_file" ] || [ "$(tr -d '\n\r' < "$cpu_file" 2>/dev/null)" != "$serial" ]; then
            printf '%s\n' "$serial" > "$cpu_file" 2>/dev/null || true
        fi
    fi

    if [ ! -s "$cpu_file" ]; then
        log "cpuinfo.txt missing, keep default Wi-Fi/BLE identity"
        return 0
    fi
    if ! command -v sha256sum >/dev/null 2>&1; then
        log "sha256sum missing, keep default Wi-Fi/BLE identity"
        return 0
    fi

    uuid="$(tr -d '\n\r' < "$cpu_file" | sha256sum | awk '{print $1}' | tail -c 6)"
    [ -n "$uuid" ] || return 0
    ssid="Xbt-F-$uuid"
    password="$(printf '%s' "${ssid}DragonflySalt" | sha256sum | cut -c1-11)"
    [ -n "$password" ] || return 0

    cp -f "$CONF_DIR/wps_hostapd.conf" /tmp/wps_hostapd.conf 2>/dev/null || true
    if [ ! -f /tmp/wps_hostapd.conf ]; then
        {
            echo "driver=nl80211"
            echo "interface=wlan1"
            echo "ssid=Xbt-F-000000"
            echo "wpa=3"
            echo "wpa_key_mgmt=WPA-PSK"
            echo "wpa_pairwise=CCMP"
            echo "wpa_passphrase=cd20d767bbe"
            echo "rsn_pairwise=CCMP"
        } > /tmp/wps_hostapd.conf
    fi

    sed -i "s#^ssid=.*#ssid=${ssid}#" /tmp/wps_hostapd.conf 2>/dev/null || true
    if ! grep -q '^ssid=' /tmp/wps_hostapd.conf 2>/dev/null; then
        echo "ssid=${ssid}" >> /tmp/wps_hostapd.conf
    fi
    sed -i "s#^wpa_passphrase=.*#wpa_passphrase=${password}#" /tmp/wps_hostapd.conf 2>/dev/null || true
    if ! grep -q '^wpa_passphrase=' /tmp/wps_hostapd.conf 2>/dev/null; then
        echo "wpa_passphrase=${password}" >> /tmp/wps_hostapd.conf
    fi

    log "Wi-Fi/BLE identity ssid=$ssid"
}

init_wifi_ap()
{
    if [ "${FACTORY_ENABLE_WIFI_AP:-0}" = "0" ]; then
        return 0
    fi

    prepare_wifi_identity
    if command -v iw >/dev/null 2>&1 && ifconfig wlan0 >/dev/null 2>&1; then
        iw dev wlan0 interface add wlan1 type __ap 2>/dev/null || true
    fi
    ifconfig wlan1 192.168.5.1 up 2>/dev/null || true
    if command -v hostapd >/dev/null 2>&1 && [ -f /tmp/wps_hostapd.conf ] && ! pidof hostapd >/dev/null 2>&1; then
        hostapd -iwlan1 -t /tmp/wps_hostapd.conf >> "$LOG_DIR/hostapd.log" 2>&1 < /dev/null &
        echo $! > "$RUN_DIR/hostapd.pid"
    fi
}

start_factory_test()
{
    if pidof factory_test >/dev/null 2>&1; then
        log "factory_test already running"
        return 0
    fi

    log "start factory_test"
    if command -v setsid >/dev/null 2>&1; then
        setsid "$BIN_DIR/factory_test" >> "$LOG_DIR/factory_test.log" 2>&1 < /dev/null &
    elif command -v nohup >/dev/null 2>&1; then
        nohup "$BIN_DIR/factory_test" >> "$LOG_DIR/factory_test.log" 2>&1 < /dev/null &
    else
        (trap '' HUP; "$BIN_DIR/factory_test" >> "$LOG_DIR/factory_test.log" 2>&1 < /dev/null) &
    fi
    echo $! > "$RUN_DIR/factory_test.pid"
}

stop_all()
{
    kill_by_pidfile "$RUN_DIR/factory_test.pid"
    kill_by_pidfile "$RUN_DIR/hostapd.pid"
    kill_by_pidfile "$RUN_DIR/bluetoothd.pid"
    kill_by_pidfile "$RUN_DIR/hciattach.pid"
    kill_by_pidfile "$RUN_DIR/mosquitto.pid"
    kill_by_pidfile "$RUN_DIR/rkaiq_3A_server.pid"
    killall nginx >/dev/null 2>&1 || true
    killall rkaiq_3A_server >/dev/null 2>&1 || true
    "$SCRIPT_DIR/factory_rndis.sh" stop >/dev/null 2>&1 || true
}

start_all()
{
    ensure_dirs
    stop_conflicting_processes
    hwclock --hctosys >/dev/null 2>&1 || true
    load_modules
    start_rndis
    start_mqtt
    start_dbus
    start_nginx
    prepare_wifi_identity
    start_rkaiq
    init_bluetooth
    init_wifi_ap
    start_factory_test
    log "factory firmware startup done"
}

start_bluetooth_only()
{
    ensure_dirs
    start_dbus
    init_bluetooth
}

case "${1:-start}" in
    start)
        start_all
        ;;
    bluetooth)
        start_bluetooth_only
        ;;
    stop)
        stop_all
        ;;
    restart)
        stop_all
        sleep 1
        start_all
        ;;
    *)
        echo "Usage: $0 {start|bluetooth|stop|restart}" >&2
        exit 1
        ;;
esac
