#!/bin/sh
set -u

OEM_DIR="${FACTORY_OEM_DIR:-/oem}"
USR_DIR="$OEM_DIR/usr"
BIN_DIR="$USR_DIR/bin"
CONF_DIR="$USR_DIR/conf"
SCRIPT_DIR="$USR_DIR/scripts"
LIB_DIR="$USR_DIR/lib"
LOG_DIR="${FACTORY_LOG_DIR:-/userdata/logs}"
RUN_DIR=/var/run/factory_fw
FACTORY_FLAG=/userdata/factory_mode

export PATH="$BIN_DIR:/usr/bin:/usr/sbin:/bin:/sbin:$PATH"
export LD_LIBRARY_PATH="$LIB_DIR:/usr/lib:/lib:${LD_LIBRARY_PATH:-}"
export FACTORY_PLATFORM_JSON="${FACTORY_PLATFORM_JSON:-$CONF_DIR/platform.json}"
export FACTORY_TESTS_JSON="${FACTORY_TESTS_JSON:-$CONF_DIR/tests.json}"

log()
{
    echo "[factory_start] $*"
}

ensure_dirs()
{
    mkdir -p "$LOG_DIR" "$RUN_DIR" /userdata/prod /device_data /tmp
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
    "$broker" -c "$conf" > "$LOG_DIR/mosquitto.log" 2>&1 &
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

wait_for_hci0()
{
    timeout="${1:-10}"
    while [ "$timeout" -gt 0 ]; do
        if hciconfig hci0 >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        timeout=$((timeout - 1))
    done
    return 1
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
    "$bt" $args >> "$LOG_DIR/bluetoothd.log" 2>&1 &
    echo $! > "$RUN_DIR/bluetoothd.pid"
    sleep 1
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

    if [ -w /sys/class/rfkill/rfkill0/state ]; then
        echo 0 > /sys/class/rfkill/rfkill0/state 2>/dev/null || true
        echo 1 > /sys/class/rfkill/rfkill0/state 2>/dev/null || true
    fi

    if command -v wifibt-init.sh >/dev/null 2>&1; then
        wifibt-init.sh start_bt >> "$LOG_DIR/bluetooth.log" 2>&1 || log "wifibt-init start_bt failed"
    fi

    if ! hciconfig hci0 >/dev/null 2>&1 && [ -e /dev/ttyS4 ]; then
        if command -v hciattach >/dev/null 2>&1; then
            hciattach /dev/ttyS4 qca 3000000 flow >> "$LOG_DIR/bluetooth.log" 2>&1 &
            echo $! > "$RUN_DIR/hciattach.pid"
        fi
    fi

    if ! wait_for_hci0 10; then
        log "hci0 not found, skip bluetoothd startup"
        return 0
    fi

    if command -v hciconfig >/dev/null 2>&1; then
        hciconfig hci0 up >> "$LOG_DIR/bluetooth.log" 2>&1 || log "hci0 up failed"
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

init_wifi_ap()
{
    if [ "${FACTORY_ENABLE_WIFI_AP:-0}" = "0" ]; then
        return 0
    fi

    cp -f "$CONF_DIR/wps_hostapd.conf" /tmp/wps_hostapd.conf 2>/dev/null || true
    if command -v iw >/dev/null 2>&1 && ifconfig wlan0 >/dev/null 2>&1; then
        iw dev wlan0 interface add wlan1 type __ap 2>/dev/null || true
    fi
    ifconfig wlan1 192.168.5.1 up 2>/dev/null || true
    if command -v hostapd >/dev/null 2>&1 && [ -f /tmp/wps_hostapd.conf ] && ! pidof hostapd >/dev/null 2>&1; then
        hostapd -iwlan1 -t /tmp/wps_hostapd.conf >> "$LOG_DIR/hostapd.log" 2>&1 &
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
    "$BIN_DIR/factory_test" >> "$LOG_DIR/factory_test.log" 2>&1 &
    echo $! > "$RUN_DIR/factory_test.pid"
}

stop_all()
{
    kill_by_pidfile "$RUN_DIR/factory_test.pid"
    kill_by_pidfile "$RUN_DIR/hostapd.pid"
    kill_by_pidfile "$RUN_DIR/bluetoothd.pid"
    kill_by_pidfile "$RUN_DIR/hciattach.pid"
    kill_by_pidfile "$RUN_DIR/mosquitto.pid"
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
    init_bluetooth
    init_wifi_ap
    start_factory_test
    log "factory firmware startup done"
}

case "${1:-start}" in
    start)
        start_all
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
        echo "Usage: $0 {start|stop|restart}" >&2
        exit 1
        ;;
esac
