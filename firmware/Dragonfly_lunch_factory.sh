#!/bin/bash
# Factory test firmware boot script
# Starts: WiFi AP → factory test app (mosquitto via S51otaupdate/start_mqtt.sh)
# Does NOT start formal apps (misc_app, multi_media, lvgl, etc.)

export TARGET_DIR="/oem"
export LD_LIBRARY_PATH=$TARGET_DIR/usr/lib:/lib:$LD_LIBRARY_PATH
export PATH=$TARGET_DIR/usr/bin:/bin:$PATH
COUNTRY_CODE=${COUNTRY_CODE:-CN}

# Sync RTC to system time
hwclock --hctosys

# Log directories
mkdir -p /userdata/logs/
mkdir -p /userdata/coredump/
ulimit -c unlimited
echo "/userdata/coredump/core-%e-%p-%t" > /proc/sys/kernel/core_pattern

# Delete old logs (>7 days) in background
find /userdata/logs -type f -mtime +7 -exec rm {} + &
find /userdata/coredump -type f -mtime +7 -exec rm {} + &

current_timestamp=$(date +"%Y-%m-%d-%H%M%S")
dmesg > "/userdata/logs/kmsg-${current_timestamp}-0.log" &

# ---- Phase 1: Load kernel modules ----
echo "[factory] Loading kernel modules..." > /dev/kmsg
modprobe aic8800_fdrv.ko aicwf_dbg_level=0x403
insmod $TARGET_DIR/usr/conf/motor_tmi8152.ko

# ---- Phase 2: Init WiFi ----
echo "[factory] Initializing WiFi..." > /dev/kmsg
udevadm control --reload-rules
udevadm trigger --action=add --subsystem-match=net
cp -f $TARGET_DIR/usr/conf/wps_hostapd.conf /tmp/
source $TARGET_DIR/usr/scripts/dragonfly_wifi_init.sh init >> /dev/kmsg

# ---- Phase 3: Start WiFi AP ----
echo "[factory] Starting WiFi AP..." > /dev/kmsg
source $TARGET_DIR/usr/scripts/dragonfly_wifi_init.sh start >> /dev/kmsg
sleep 1

# ---- Phase 4: Setup USB gadget (ADB) ----
# S50usb-gadget.sh (priority 50) already started the usb-gadget service before us
# (priority 90). That service defaults to USB_FUNCS=adb and starts adbd once the
# UDC appears.  It runs asynchronously — the daemon may still be configuring when
# we get here.  Do NOT tear down configfs and re-configure with usb_config.sh
# unless the service genuinely failed, otherwise we race with the daemon and
# break ADB.
echo "[factory] Waiting for USB gadget (ADB)..." > /dev/kmsg

# Wait for a UDC to appear (DWC3 may probe late)
for i in $(seq 1 15); do
    udc_dev=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    [ -n "$udc_dev" ] && break
    sleep 1
done

if [ -z "$udc_dev" ]; then
    echo "[factory] No UDC after 15s, ADB unavailable" > /dev/kmsg
else
    echo "[factory] UDC $udc_dev present, waiting for usb-gadget service..." > /dev/kmsg
    # Wait for the usb-gadget daemon to finish configuring and start adbd.
    # The daemon does: mount configfs → create gadget → configure adb → bind UDC
    # → start adbd.  Give it up to 20 s.
    for i in $(seq 1 20); do
        if pidof adbd > /dev/null 2>&1; then
            echo "[factory] adbd started by usb-gadget service" > /dev/kmsg
            break
        fi
        sleep 1
    done

    if ! pidof adbd > /dev/null 2>&1; then
        echo "[factory] usb-gadget service did not start adbd, trying manual fallback..." > /dev/kmsg
        if [ -x $TARGET_DIR/usr/scripts/usb_config.sh ]; then
            # Only tear down when the service genuinely failed
            if [ -e /sys/kernel/config/usb_gadget/rockchip/UDC ]; then
                echo "" > /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null
            fi
            umount /dev/usb-ffs/adb 2>/dev/null
            umount /sys/kernel/config 2>/dev/null
            rm -rf /sys/kernel/config/usb_gadget/rockchip 2>/dev/null
            rm -rf /dev/usb-ffs 2>/dev/null
            $TARGET_DIR/usr/scripts/usb_config.sh >> /dev/kmsg 2>&1 &
        else
            echo "[factory] usb_config.sh not found, ADB unavailable" > /dev/kmsg
        fi
    fi

    # The usb-gadget daemon may start adbd without successfully binding
    # the gadget to the UDC (e.g. UDC not ready when bind attempted).
    # If configfs gadget exists but is not bound, bind it now.
    if [ -n "$udc_dev" ] && [ -e /sys/kernel/config/usb_gadget/rockchip/UDC ]; then
	bound_udc=$(cat /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null)
	if [ -z "$bound_udc" ]; then
	    echo "[factory] Gadget not bound to UDC, binding $udc_dev..." > /dev/kmsg
	    echo "$udc_dev" > /sys/kernel/config/usb_gadget/rockchip/UDC 2>/dev/null || \
		echo "[factory] Failed to bind $udc_dev" > /dev/kmsg
	fi
    fi
fi

# ---- Phase 5: Start factory test app ----
# (mosquitto is already started by S51otaupdate via start_mqtt.sh)
echo "[factory] Starting factory test application..." > /dev/kmsg

# Create test output directories
mkdir -p /userdata/prod/pics/4663
mkdir -p /userdata/prod/pics/678

# Start optional iperf3 server (for network test)
$TARGET_DIR/usr/scripts/iperf3_server_safe.sh &

# Start factory test firmware
Falcon_Air_Factory 2>&1 | tee -a /userdata/logs/factory_test.log &

echo "[factory] Boot complete." > /dev/kmsg
