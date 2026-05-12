#!/bin/bash

check_bt_chip() {
    if grep -rqi bluetooth /sys/class/rfkill/rfkill*/type 2>/dev/null; then
        return 0
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

BT_RFKILL=$(grep -l "bluetooth" /sys/class/rfkill/rfkill*/type 2>/dev/null | head -1 | sed 's/\/type//')
if [ -n "$BT_RFKILL" ] && [ -w "$BT_RFKILL/state" ]; then
    echo 0 > "$BT_RFKILL/state"
    echo 1 > "$BT_RFKILL/state"
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
