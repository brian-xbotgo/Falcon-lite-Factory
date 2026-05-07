#pragma once

// BLE factory test HAL — advertises device SN over BLE + minimal GATT server
// Listens on MQTT 10R/31R for SN, then starts advertising via BlueZ D-Bus

#include <cstdint>

namespace ft {

class BleAdvertiser {
public:
    BleAdvertiser() = default;
    ~BleAdvertiser();

    // Non-copyable
    BleAdvertiser(const BleAdvertiser&) = delete;
    BleAdvertiser& operator=(const BleAdvertiser&) = delete;

    // Start MQTT + BLE event loop (blocking — run in a thread)
    int run();

    // Signal shutdown from another thread
    void shutdown();

    // Public for C callback access
    void* m_mosq = nullptr;
    void* m_conn = nullptr;
    void* m_loop = nullptr;
    bool  m_running = false;
    bool  m_adv_registered = false;
    bool  m_gatt_registered = false;
    bool  m_sn_valid = false;
    uint8_t m_sn[14] = {};

    int  setupMqtt();
    int  setupDbus();
    int  registerAdvertisement();
    int  registerGattServer();
};

} // namespace ft
