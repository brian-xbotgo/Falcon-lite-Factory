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
# (sleep removed — hostapd loop already checks AP ENABLED before returning)

# ---- Phase 3b: Init Bluetooth HCI ----
echo "[factory] Initializing Bluetooth..." > /dev/kmsg
source $TARGET_DIR/usr/scripts/dragonfly_bt_init.sh >> /dev/kmsg
# (sleep removed — BlueZ wait below handles readiness)

# ---- Phase 4: Start ISP 3A server (rkaiq) ----
# rkaiq initializes rkisp pipeline, required for camera capture
echo "[factory] Starting rkaiq 3A server..." > /dev/kmsg

# Kill any stale instances and clean up leftover socket/lock files
killall -9 rkaiq_3A_server 2>/dev/null || true
rm -f /tmp/.rkaiq_3A /tmp/aiq0.lock /tmp/aiq1.lock /var/tmp/rkipc
killall -9 rkipc 2>/dev/null || true
ipcrm -a 2>/dev/null || true
sleep 1

rkaiq_3A_server &

# Poll-wait: ISP pipeline must be ready before proceeding
# rkaiq prepares dual ISP (media3=cam0, media4=cam1);
# we check that rkisp_mainpath nodes accept basic V4L2 query
for i in $(seq 1 15); do
    sleep 1
    if v4l2-ctl -d /dev/video23 -D 2>/dev/null | grep -q rkisp_mainpath && \
       v4l2-ctl -d /dev/video31 -D 2>/dev/null | grep -q rkisp_mainpath; then
        echo "[factory] rkaiq ISP pipeline ready (took ${i}s)" > /dev/kmsg
        break
    fi
    # rkaiq segfaulted? restart it
    if ! killall -0 rkaiq_3A_server 2>/dev/null; then
        echo "[factory] rkaiq died, restarting..." > /dev/kmsg
        rkaiq_3A_server &
    fi
done

echo "[factory] rkaiq 3A server started" > /dev/kmsg

# ---- Phase 5: Setup USB gadget (ADB + RNDIS) ----
# usb_gadget_health.sh handles initial setup + ongoing health monitoring
source $TARGET_DIR/usr/scripts/usb_gadget_health.sh

# ---- Phase 6: Start BLE + factory test app ----
# (mosquitto is already started by S51otaupdate via start_mqtt.sh)
echo "[factory] Starting BLE advertisement + factory test..." > /dev/kmsg

# Wait for BlueZ to be fully ready
echo "[factory] Waiting for BlueZ to be ready..." > /dev/kmsg
for i in $(seq 1 30); do
    if busctl list 2>/dev/null | grep -q "org.bluez"; then
        echo "[factory] BlueZ is ready" > /dev/kmsg
        break
    fi
    sleep 0.5
done

# Create required directories
mkdir -p /device_data
mkdir -p /userdata/prod/pics/4663
mkdir -p /userdata/prod/pics/678

# Start BLE SN advertising (listens on MQTT 10R/31R, broadcasts SN over BLE)
$TARGET_DIR/usr/bin/ble_factory_advertise 2>&1 | tee -a /userdata/logs/ble_factory.log &

# Start optional iperf3 server (for network test)
$TARGET_DIR/usr/scripts/iperf3_server_safe.sh &

# Start factory test firmware
Falcon_Air_Factory 2>&1 | tee -a /userdata/logs/factory_test.log &

echo "[factory] Boot complete." > /dev/kmsg
