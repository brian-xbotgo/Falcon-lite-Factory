#!/bin/sh
set -u

GADGET_DIR=/sys/kernel/config/usb_gadget/rockchip
CONFIG_DIR="$GADGET_DIR/configs/b.1"
FUNCTION_DIR="$GADGET_DIR/functions"
STRINGS_DIR="$GADGET_DIR/strings/0x409"
CONFIG_STRINGS_DIR="$CONFIG_DIR/strings/0x409"
ADB_FFS_DIR=/dev/usb-ffs/adb
USB_MODE_FILE=/device_data/factory_usb_mode
PIDFILE=/var/run/factory_usb_health.pid
RNDIS_IP="${FACTORY_RNDIS_IP:-172.16.110.6}"
PRODUCT="${FACTORY_USB_PRODUCT:-Factory ADB RNDIS}"
ADB_RNDIS_PID="${FACTORY_USB_ADB_RNDIS_PID:-0x0013}"
USB_HEALTH_INTERVAL="${FACTORY_USB_HEALTH_INTERVAL:-3}"

if [ -n "${FACTORY_USB_MODE:-}" ]; then
    USB_MODE="$FACTORY_USB_MODE"
elif [ -f "$USB_MODE_FILE" ]; then
    USB_MODE="$(head -n 1 "$USB_MODE_FILE" | tr -d ' \r\n')"
    [ -n "$USB_MODE" ] || USB_MODE=adb_rndis
else
    USB_MODE=adb_rndis
fi
USB_MODE="$(echo "$USB_MODE" | tr '-' '_')"
case "$USB_MODE" in
    adb|rndis|adb_rndis|rndis_adb)
        ;;
    *)
        echo "[factory_usb] unsupported FACTORY_USB_MODE=$USB_MODE, fallback to adb_rndis"
        USB_MODE=adb_rndis
        ;;
esac

log()
{
    echo "[factory_usb] $*"
}

kmsg_log()
{
    if [ -w /dev/kmsg ]; then
        echo "[factory_usb] $*" > /dev/kmsg 2>/dev/null || true
    fi
    log "$*"
}

write_if_exists()
{
    file="$1"
    value="$2"
    if [ -e "$file" ]; then
        echo "$value" > "$file"
    fi
}

safe_unlink()
{
    path="$1"
    [ -e "$path" ] || [ -L "$path" ] || return 0
    rm -f "$path" 2>/dev/null || true
}

bring_usb0_up()
{
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if ifconfig usb0 >/dev/null 2>&1; then
            ifconfig usb0 "$RNDIS_IP" up
            log "usb0 up ip=$RNDIS_IP"
            return 0
        fi
        sleep 1
    done
    log "usb0 not found"
    return 1
}

mode_has_adb()
{
    case "$USB_MODE" in
        adb|adb_rndis|rndis_adb)
            return 0
            ;;
    esac
    return 1
}

mode_has_rndis()
{
    case "$USB_MODE" in
        rndis|adb_rndis|rndis_adb)
            return 0
            ;;
    esac
    return 1
}

id_product_for_mode()
{
    case "$USB_MODE" in
        adb)
            echo 0x0006
            ;;
        rndis)
            echo 0x0003
            ;;
        adb_rndis|rndis_adb)
            echo "$ADB_RNDIS_PID"
            ;;
        *)
            echo "$ADB_RNDIS_PID"
            ;;
    esac
}

find_udc()
{
    ls /sys/class/udc 2>/dev/null | head -n 1
}

prepare_adb_env()
{
    if [ -f /oem/usr/etc/profile.d/adbd.sh ]; then
        . /oem/usr/etc/profile.d/adbd.sh
    elif [ -f /etc/profile.d/adbd.sh ]; then
        . /etc/profile.d/adbd.sh
    fi

    [ -n "${ADB_SECURE:-}" ] || export ADB_SECURE=1
    if [ -z "${ADBD_SHELL:-}" ] && [ -x /bin/bash ]; then
        export ADBD_SHELL=/bin/bash
    fi

    if [ -f /oem/usr/adb_keys ]; then
        if [ ! -f /adb_keys ] || ! cmp -s /oem/usr/adb_keys /adb_keys; then
            cp -f /oem/usr/adb_keys /adb_keys 2>/dev/null || true
        fi
    fi
    [ -f /adb_keys ] && chmod 644 /adb_keys 2>/dev/null || true
}

start_adbd()
{
    if ! mode_has_adb; then
        return 0
    fi

    prepare_adb_env

    mkdir -p /dev/usb-ffs "$ADB_FFS_DIR"
    if ! mountpoint -q "$ADB_FFS_DIR" 2>/dev/null; then
        mount -o uid=2000,gid=2000 -t functionfs adb "$ADB_FFS_DIR" || {
            log "mount FunctionFS adb failed"
            return 1
        }
    fi

    if pidof adbd >/dev/null 2>&1 && [ -e "$ADB_FFS_DIR/ep1" ] && [ -e "$ADB_FFS_DIR/ep2" ]; then
        log "adbd already running"
        return 0
    fi
    killall adbd >/dev/null 2>&1 || true

    if [ -x /usr/bin/adbd ]; then
        /usr/bin/adbd &
        log "start adbd from /usr/bin/adbd"
    elif [ -x /oem/usr/bin/adbd ]; then
        /oem/usr/bin/adbd &
        log "start adbd from /oem/usr/bin/adbd"
    elif command -v adbd >/dev/null 2>&1; then
        adbd &
        log "start adbd from PATH"
    else
        log "adbd not found"
        return 1
    fi

    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if [ -e "$ADB_FFS_DIR/ep1" ] && [ -e "$ADB_FFS_DIR/ep2" ]; then
            log "adb FunctionFS ready"
            return 0
        fi
        sleep 1
    done

    log "adb FunctionFS endpoints not ready"
    return 1
}

usb_is_configured()
{
    udc="$(find_udc)"
    [ -n "$udc" ] || return 1
    [ -e "/sys/class/udc/$udc/state" ] || return 1
    [ "$(cat "/sys/class/udc/$udc/state" 2>/dev/null)" = "configured" ] || return 1
    [ -e "$GADGET_DIR/UDC" ] || return 1
    [ -n "$(cat "$GADGET_DIR/UDC" 2>/dev/null)" ] || return 1
    return 0
}

adb_is_healthy()
{
    mode_has_adb || return 0
    pidof adbd >/dev/null 2>&1 || return 1
    [ -e "$ADB_FFS_DIR/ep1" ] && [ -e "$ADB_FFS_DIR/ep2" ] || return 1
    return 0
}

rndis_is_healthy()
{
    mode_has_rndis || return 0
    ifconfig usb0 >/dev/null 2>&1 || return 1
    ifconfig usb0 2>/dev/null | grep -q "$RNDIS_IP" || return 1
    return 0
}

usb_health_ok()
{
    usb_is_configured && adb_is_healthy && rndis_is_healthy
}

repair_usb()
{
    udc="$(find_udc)"
    [ -n "$udc" ] || {
        kmsg_log "USB repair skipped: no UDC"
        return 1
    }

    kmsg_log "Repair USB gadget udc=$udc mode=$USB_MODE"
    echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    sleep 1

    if mode_has_adb; then
        killall adbd >/dev/null 2>&1 || true
        start_adbd || true
    fi

    echo "$udc" > "$GADGET_DIR/UDC" 2>/dev/null || {
        kmsg_log "USB repair bind failed: $udc"
        return 1
    }

    if mode_has_rndis; then
        bring_usb0_up || true
    fi

    kmsg_log "USB repair complete"
}

stop_health_monitor()
{
    if [ -f "$PIDFILE" ]; then
        old_pid="$(cat "$PIDFILE" 2>/dev/null || true)"
        if [ -n "${old_pid:-}" ] && kill -0 "$old_pid" 2>/dev/null; then
            kill "$old_pid" 2>/dev/null || true
        fi
        rm -f "$PIDFILE"
    fi
}

start_health_monitor()
{
    [ "${FACTORY_USB_HEALTH_MONITOR:-1}" = "0" ] && return 0

    stop_health_monitor
    (
        trap 'rm -f "$PIDFILE"' EXIT
        was_healthy=0
        cooldown=0

        while true; do
            if usb_health_ok; then
                was_healthy=1
                cooldown=0
            elif [ "$was_healthy" -eq 1 ] && [ "$cooldown" -le 0 ]; then
                repair_usb || true
                cooldown=5
            elif [ "$was_healthy" -eq 0 ] && [ "$cooldown" -le 0 ]; then
                repair_usb || true
                cooldown=5
            fi

            if [ "$cooldown" -gt 0 ]; then
                cooldown=$((cooldown - 1))
            fi
            sleep "$USB_HEALTH_INTERVAL"
        done
    ) >/dev/null 2>&1 &
    echo $! > "$PIDFILE"
    log "USB health monitor started pid=$!"
}

stop_adbd()
{
    killall adbd >/dev/null 2>&1 || true
    if mountpoint -q "$ADB_FFS_DIR" 2>/dev/null; then
        umount "$ADB_FFS_DIR" 2>/dev/null || true
    fi
}

reset_gadget_links()
{
    if [ -e "$GADGET_DIR/UDC" ]; then
        echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    fi

    for link in "$CONFIG_DIR"/f* "$CONFIG_DIR"/ffs.adb "$CONFIG_DIR"/rndis.gs0; do
        safe_unlink "$link"
    done
    safe_unlink "$GADGET_DIR/os_desc/b.1"
}

start_usb()
{
    if [ ! -d /sys/kernel/config ]; then
        log "configfs path missing"
        return 1
    fi

    mountpoint -q /sys/kernel/config 2>/dev/null ||
        mount -t configfs none /sys/kernel/config 2>/dev/null || true
    mkdir -p "$GADGET_DIR" "$STRINGS_DIR" "$CONFIG_STRINGS_DIR" "$FUNCTION_DIR" "$CONFIG_DIR"

    reset_gadget_links

    write_if_exists "$GADGET_DIR/idVendor" "0x2207"
    product_id="$(id_product_for_mode)"
    write_if_exists "$GADGET_DIR/idProduct" "$product_id"
    write_if_exists "$GADGET_DIR/bcdDevice" "0x0310"
    write_if_exists "$GADGET_DIR/bcdUSB" "0x0200"
    write_if_exists "$GADGET_DIR/bDeviceClass" "239"
    write_if_exists "$GADGET_DIR/bDeviceSubClass" "2"
    write_if_exists "$GADGET_DIR/bDeviceProtocol" "1"
    write_if_exists "$GADGET_DIR/os_desc/b_vendor_code" "0x1"
    write_if_exists "$GADGET_DIR/os_desc/qw_sign" "MSFT100"

    serial="$(awk -F: '/Serial/ {gsub(/ /, "", $2); print $2; exit}' /proc/cpuinfo 2>/dev/null)"
    [ -n "${serial:-}" ] || serial="factory"
    echo "$serial" > "$STRINGS_DIR/serialnumber"
    echo "rockchip" > "$STRINGS_DIR/manufacturer"
    echo "$PRODUCT" > "$STRINGS_DIR/product"
    echo "$USB_MODE" > "$CONFIG_STRINGS_DIR/configuration"
    write_if_exists "$CONFIG_DIR/MaxPower" "500"
    [ -d "$GADGET_DIR/os_desc" ] && ln -s "$CONFIG_DIR" "$GADGET_DIR/os_desc/b.1" 2>/dev/null || true

    next_func=1

    if mode_has_rndis; then
        mkdir -p "$FUNCTION_DIR/rndis.gs0"
        echo "RNDIS" > "$FUNCTION_DIR/rndis.gs0/os_desc/interface.rndis/compatible_id" 2>/dev/null || true
        echo "5162001" > "$FUNCTION_DIR/rndis.gs0/os_desc/interface.rndis/sub_compatible_id" 2>/dev/null || true
        ln -s "$FUNCTION_DIR/rndis.gs0" "$CONFIG_DIR/f$next_func"
        next_func=$((next_func + 1))
    fi

    if mode_has_adb; then
        mkdir -p "$FUNCTION_DIR/ffs.adb"
        if ! start_adbd; then
            log "adbd init failed"
            return 1
        fi
        ln -s "$FUNCTION_DIR/ffs.adb" "$CONFIG_DIR/f$next_func"
        next_func=$((next_func + 1))
    fi

    udc="$(find_udc)"
    if [ -n "$udc" ]; then
        if ! echo "$udc" > "$GADGET_DIR/UDC" 2>/dev/null; then
            log "bind udc=$udc failed"
            return 1
        fi
        log "bound udc=$udc mode=$USB_MODE idProduct=$product_id"
    else
        log "no UDC found"
        return 1
    fi

    if mode_has_rndis; then
        bring_usb0_up
    fi

    start_health_monitor
}

stop_usb()
{
    stop_health_monitor
    ifconfig usb0 down 2>/dev/null || true
    if [ -e "$GADGET_DIR/UDC" ]; then
        echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    fi
    reset_gadget_links
    stop_adbd
}

case "${1:-start}" in
    start)
        start_usb
        ;;
    monitor)
        start_health_monitor
        ;;
    stop)
        stop_usb
        ;;
    restart)
        stop_usb
        start_usb
        ;;
    *)
        echo "Usage: $0 {start|monitor|stop|restart}" >&2
        echo "FACTORY_USB_MODE=adb_rndis|adb|rndis" >&2
        echo "Persistent mode file: $USB_MODE_FILE" >&2
        exit 1
        ;;
esac
