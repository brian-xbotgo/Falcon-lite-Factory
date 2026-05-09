// Standalone entry point for BLE WiFi configuration binary.
// Provides BLE advertisement + GATT service for phone app WiFi config,
// plus factory test SN broadcasting.
#include "ble/BleAdvertiser.h"

int main()
{
    ft::BleAdvertiser ble;
    return ble.run();
}
