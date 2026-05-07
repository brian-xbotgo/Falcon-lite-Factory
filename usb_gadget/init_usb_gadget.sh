#!/bin/bash
# Factory test USB gadget setup (ADB + RNDIS)
# Sources: consolidated from Dragonfly_lunch_factory.sh Phase 4
#
# Flow: wait UDC → wait adbd → rebind fix → RNDIS (if needed) → usb0 IP
# S50usb-gadget (priority 50) runs first, configures based on /etc/.usb_config
# This script (priority 90) guards against UDC bind failures

TARGET_DIR="${TARGET_DIR:-/oem}"
echo "[factory] Waiting for USB gadget (ADB)..." > /dev/kmsg

# ---- Wait for UDC ----
for i in $(seq 1 15); do
    udc_dev=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    [ -n "$udc_dev" ] && break
    sleep 1
done

if [ -z "$udc_dev" ]; then
    echo "[factory] No UDC after 15s, ADB unavailable" > /dev/kmsg
    exit 0
fi

echo "[factory] UDC $udc_dev present, waiting for usb-gadget service..." > /dev/kmsg

# ---- Wait for adbd (started by S50usb-gadget) ----
for i in $(seq 1 20); do
    if pidof adbd > /dev/null 2>&1; then
        echo "[factory] adbd started by usb-gadget service" > /dev/kmsg
        break
    fi
    sleep 1
done

# ---- Fallback: usb-gadget service failed entirely ----
if ! pidof adbd > /dev/null 2>&1; then
    echo "[factory] usb-gadget service did not start adbd, trying manual fallback..." > /dev/kmsg
    if [ -x $TARGET_DIR/usr/scripts/usb_config.sh ]; then
        if [ -e /sys/kernel/config/usb_gadget/rockchip/UDC ]; then
            echo "" > /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null
        fi
        umount /dev/usb-ffs/adb 2>/dev/null
        umount /sys/kernel/config 2>/dev/null
        rm -rf /sys/kernel/config/usb_gadget/rockchip 2>/dev/null
        rm -rf /dev/usb-ffs 2>/dev/null
        $TARGET_DIR/usr/scripts/usb_config.sh rndis >> /dev/kmsg 2>&1 &
    else
        echo "[factory] usb_config.sh not found, ADB unavailable" > /dev/kmsg
    fi
    exit 0
fi

# ---- UDC rebind fix ----
# S50usb-gadget may start adbd without successfully binding to UDC
if [ -n "$udc_dev" ] && [ -e /sys/kernel/config/usb_gadget/rockchip/UDC ]; then
    bound_udc=$(cat /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null)
    if [ -z "$bound_udc" ]; then
        echo "[factory] Gadget not bound to UDC, binding $udc_dev..." > /dev/kmsg
        echo "$udc_dev" > /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null || \
            echo "[factory] Failed to bind $udc_dev" > /dev/kmsg
    fi
fi

# ---- RNDIS ----
GADGET=/sys/kernel/config/usb_gadget/rockchip
FUNC=$GADGET/functions
CFG=$GADGET/configs/b.1
if [ -f /etc/.usb_config ]; then
    echo "[factory] /etc/.usb_config present, usb-gadget handles RNDIS" > /dev/kmsg
elif [ -d "$GADGET" ] && [ ! -e "$FUNC/rndis.gs0" ]; then
    mkdir -p $FUNC/rndis.gs0
    ln -s $FUNC/rndis.gs0 $CFG/f2 2>/dev/null
    echo "[factory] RNDIS function added to gadget" > /dev/kmsg
    if [ -n "$udc_dev" ]; then
        echo "" > $GADGET/UDC 2>/dev/null
        sleep 1
        echo "$udc_dev" > $GADGET/UDC 2>/dev/null
        echo "[factory] UDC re-bound with RNDIS" > /dev/kmsg
    fi
    killall adbd 2>/dev/null
    sleep 1
    start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
    echo "[factory] adbd restarted after RNDIS rebind" > /dev/kmsg
fi

# ---- Configure usb0 IP ----
sleep 2
ifconfig usb0 172.16.110.6 2>/dev/null && \
    ifconfig usb0 up 2>/dev/null && \
    echo "[factory] usb0 configured: 172.16.110.6" > /dev/kmsg || \
    echo "[factory] usb0 configuration failed" > /dev/kmsg
