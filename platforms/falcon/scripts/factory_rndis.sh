#!/bin/sh
set -u

GADGET_DIR=/sys/kernel/config/usb_gadget/rockchip
CONFIG_DIR="$GADGET_DIR/configs/b.1"
FUNCTION_DIR="$GADGET_DIR/functions"
STRINGS_DIR="$GADGET_DIR/strings/0x409"
CONFIG_STRINGS_DIR="$CONFIG_DIR/strings/0x409"
RNDIS_IP="${FACTORY_RNDIS_IP:-172.16.110.6}"
PRODUCT="${FACTORY_USB_PRODUCT:-Factory RNDIS}"

log()
{
    echo "[factory_rndis] $*"
}

write_if_exists()
{
    file="$1"
    value="$2"
    if [ -e "$file" ]; then
        echo "$value" > "$file"
    fi
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

start_rndis()
{
    if [ ! -d /sys/kernel/config ]; then
        log "configfs path missing"
        return 1
    fi

    mountpoint -q /sys/kernel/config 2>/dev/null || mount -t configfs none /sys/kernel/config 2>/dev/null || true
    mkdir -p "$GADGET_DIR" "$STRINGS_DIR" "$CONFIG_STRINGS_DIR" "$FUNCTION_DIR" "$CONFIG_DIR"

    if [ -e "$GADGET_DIR/UDC" ]; then
        echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    fi

    write_if_exists "$GADGET_DIR/idVendor" "0x2207"
    write_if_exists "$GADGET_DIR/idProduct" "0x0013"
    write_if_exists "$GADGET_DIR/bcdDevice" "0x0310"
    write_if_exists "$GADGET_DIR/bcdUSB" "0x0200"
    write_if_exists "$GADGET_DIR/bDeviceClass" "239"
    write_if_exists "$GADGET_DIR/bDeviceSubClass" "2"
    write_if_exists "$GADGET_DIR/bDeviceProtocol" "1"

    serial="$(awk -F: '/Serial/ {gsub(/ /, "", $2); print $2; exit}' /proc/cpuinfo 2>/dev/null)"
    [ -n "${serial:-}" ] || serial="factory"
    echo "$serial" > "$STRINGS_DIR/serialnumber"
    echo "rockchip" > "$STRINGS_DIR/manufacturer"
    echo "$PRODUCT" > "$STRINGS_DIR/product"
    echo "rndis" > "$CONFIG_STRINGS_DIR/configuration"
    write_if_exists "$CONFIG_DIR/MaxPower" "500"

    mkdir -p "$FUNCTION_DIR/rndis.gs0"
    if [ ! -e "$CONFIG_DIR/f1" ]; then
        ln -s "$FUNCTION_DIR/rndis.gs0" "$CONFIG_DIR/f1"
    fi

    udc="$(ls /sys/class/udc 2>/dev/null | head -n 1)"
    if [ -n "$udc" ]; then
        echo "$udc" > "$GADGET_DIR/UDC"
        log "bound udc=$udc"
    else
        log "no UDC found"
        return 1
    fi

    bring_usb0_up
}

stop_rndis()
{
    ifconfig usb0 down 2>/dev/null || true
    if [ -e "$GADGET_DIR/UDC" ]; then
        echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    fi
}

case "${1:-start}" in
    start)
        start_rndis
        ;;
    stop)
        stop_rndis
        ;;
    restart)
        stop_rndis
        start_rndis
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}" >&2
        exit 1
        ;;
esac
