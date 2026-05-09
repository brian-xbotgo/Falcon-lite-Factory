#!/bin/bash

check_bt_chip() {
    if [ -e /sys/class/rfkill/rfkill0/type ]; then
        if grep -qi bluetooth /sys/class/rfkill/rfkill0/type; then
            return 0
        fi
    fi

    if [ -e /dev/ttyS2 ]; then
        return 0
    fi

    return 1
}

wait_for_hci0() {
    local timeout=10
    while ((timeout > 0)); do
        if hciconfig | grep -q hci0; then
            return 0
        fi
        sleep 1
        ((timeout--))
    done
    return 1
}

if ! check_bt_chip; then
    echo "No Bluetooth chip found"
    return 1
fi

echo "Bluetooth chip detected, initializing..."

if [ -w /sys/class/rfkill/rfkill0/state ]; then
    echo 0 > /sys/class/rfkill/rfkill0/state
    echo 1 > /sys/class/rfkill/rfkill0/state
fi

hciattach /dev/ttyS2 any 1500000 flow &> /dev/null &

if ! wait_for_hci0; then
    # Retry: kill and restart hciattach
    killall hciattach 2>/dev/null
    sleep 1
    hciattach /dev/ttyS2 any 1500000 flow &> /dev/null &
    if ! wait_for_hci0; then
        echo "Timeout waiting for hci0 to appear"
        return 1
    fi
fi

hciconfig hci0 up
echo "Bluetooth interface hci0 is up"

# Restart bluetoothd with experimental mode (required for LEAdvertisingManager1 / GattManager1)
if killall bluetoothd 2>/dev/null; then
    sleep 1
fi
/usr/libexec/bluetooth/bluetoothd -E -n -d &
sleep 1
echo "bluetoothd restarted with experimental mode"
