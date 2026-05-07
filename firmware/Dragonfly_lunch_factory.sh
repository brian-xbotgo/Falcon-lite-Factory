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

# ---- Phase 3b: Init Bluetooth HCI ----
echo "[factory] Initializing Bluetooth..." > /dev/kmsg
source $TARGET_DIR/usr/scripts/dragonfly_bt_init.sh >> /dev/kmsg
sleep 1

# ---- Phase 4: Setup USB gadget (ADB + RNDIS) ----
# See usb_gadget/init_usb_gadget.sh for the full implementation
source $TARGET_DIR/usr/scripts/init_usb_gadget.sh

# ---- Phase 5: Start BLE + factory test app ----
# (mosquitto is already started by S51otaupdate via start_mqtt.sh)
echo "[factory] Starting BLE advertisement + factory test..." > /dev/kmsg

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
