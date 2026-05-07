#!/bin/bash

TS=$(date +"%Y%m%d-%H%M%S")

# 可配置国家码（默认CN）
COUNTRY_CODE=${COUNTRY_CODE:-CN}

# 检查是否存在 WiFi 芯片
check_wifi_chip() {
    echo "Checking for WiFi chip..."

    if [ -d /sys/bus/sdio/devices/ ] && [ -n "$(ls -A /sys/bus/sdio/devices/)" ]; then
	return 0
    fi

    if command -v iw >/dev/null && iw dev | grep -q Interface; then
        return 0
    fi

    echo "No WiFi chip detected. Abort."
    return 1
}

# 等待指定的网络设备出现
wait_for_net_device() {
    local dev_name="$1"
    local timeout=10

    while ((timeout > 0)); do
        if ifconfig -a | grep -q "$dev_name"; then
            return 0
        else
            sleep 1
            ((timeout--))
        fi
    done

    return 1
}

if ! check_wifi_chip; then
    return 1
fi

# 配置 USB 网络设备 (如果存在)
if ifconfig -a | grep -q "usb0"; then
    echo "Configuring usb0 interface..."
    ifconfig usb0 172.16.110.6
    ifconfig usb0 up
    echo "usb0 configured with IP 172.16.110.6"
fi

# 设置国家码
if command -v iw >/dev/null; then
    iw reg set "$COUNTRY_CODE"
    echo "Set WiFi country code to $COUNTRY_CODE"
fi

if [ "$1" == "init" ]; then
    if wait_for_net_device "wlan0"; then
        ifconfig wlan0 up
        LOG=/userdata/logs/wpa_supplicant-wlan0-$TS.log
        wpa_supplicant -Dnl80211 -iwlan0 -c/etc/wpa_supplicant.conf -t -d -f "$LOG" &
    else
        echo "wlan0 device not found."
        return 1
    fi

elif [ "$1" == "start" ]; then
    iw dev wlan0 interface add wlan1 type __ap
    if wait_for_net_device "wlan1"; then
        ifconfig wlan1 up
        ifconfig wlan1 192.168.5.1

        TS=$(date +"%Y%m%d-%H%M%S")
        LOG=/userdata/logs/hostapd-wlan1-$TS.log
        if [ -f /tmp/wps_hostapd.conf ]; then
            if [ -f /userdata/cpuinfo.txt ]; then
                uuid=$(tr -d '\n\r' < /userdata/cpuinfo.txt | sha256sum | awk '{print $1}'  | tr -d '\n'  | tail -c 6)
                ssid=Xbt-F-${uuid}
                sed -i "s#^ssid=Xbt-F-.*#ssid=${ssid}#" /tmp/wps_hostapd.conf
                password=$(echo -n "${ssid}DragonflySalt" | sha256sum | cut -c1-11)
                sed -i "s/^wpa_passphrase=.*/wpa_passphrase=${password}/" /tmp/wps_hostapd.conf
            else
                echo "cpuinfo.txt missing"
            fi
            hostapd -iwlan1 -t /tmp/wps_hostapd.conf >> $LOG 2>&1 &
        else
            hostapd -iwlan1 -t /oem/usr/conf/wps_hostapd.conf >> $LOG 2>&1 &
            echo "/tmp/wps_hostapd.conf missing, use original one"
        fi
        echo "Waiting for AP to be ENABLED..."
        for i in {1..10}; do
            state=$(hostapd_cli -i wlan1 status | grep state=)
            if echo "$state" | grep -q "ENABLED"; then
                echo "AP ready!"
                break
            fi
            sleep 1
        done
        echo "start ap service done"
    else
        echo "no wlan1 device"
        return 1
    fi

elif [ "$1" == "stop" ]; then
    killall -9 hostapd
    ifconfig wlan1 down
    echo "stop ap service done"
else
    echo "Usage: $0 {init|start|stop}"
    return 1
fi
