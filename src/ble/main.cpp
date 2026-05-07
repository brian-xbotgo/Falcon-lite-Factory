// Standalone entry point for BLE factory test binary.
// This is a separate process from Falcon_Air_Factory.
#include "hal/BleAdvertiser.h"

int main() {
    ft::BleAdvertiser ble;
    return ble.run();
}
