#pragma once

#include <cstdint>
#include <memory>
#include <gio/gio.h>
#include "ble/BleDeviceInfo.h"
#include "ble/BleWifiManager.h"
#include "ble/BleMqttBridge.h"
#include "ble/BleGattServer.h"

namespace ft {

class BleAdvertiser {
public:
    BleAdvertiser() = default;
    ~BleAdvertiser();

    // Non-copyable
    BleAdvertiser(const BleAdvertiser&) = delete;
    BleAdvertiser& operator=(const BleAdvertiser&) = delete;

    // Start everything: MQTT → D-Bus → Advertisement → GATT → main loop (blocking).
    int run();

    // Signal shutdown from another thread.
    void shutdown();

    // Exposed for GattServer callbacks and C static callbacks.
    void stopAdvertisement();
    void startAdvertisement();
    bool isFactoryMode() const { return m_factoryMode; }
    const char* getBleName()  { return m_deviceInfo.getBleName(); }
    std::vector<uint8_t> buildManufacturerData();

    BleDeviceInfo  m_deviceInfo;
    BleWifiManager m_wifi;
    BleMqttBridge  m_mqtt;
    BleGattServer  m_gatt;
    GDBusConnection* m_conn = nullptr;
    bool m_factoryMode = false;
    bool m_advRegistered = false;
    bool m_isAdvertising = false;

private:
    GMainLoop* m_loop = nullptr;

    // Manufacturer data cache (to avoid redundant updates)
    uint32_t m_cacheStaCount    = 0xFFFFFFFF;
    uint32_t m_cacheIp          = 0xFFFFFFFF;
    uint8_t  m_cacheLiveStatus  = 0xFF;
    uint8_t  m_cacheColor       = 0xFF;
    char     m_cacheAlias[16]   = {};
    uint8_t  m_cacheSn[SN_LEN]  = {};

    int setupDbus();
    void createAdvertisement();
    void registerAdvertisement();

    // 1-second timer callback for updating manufacturer data
    static gboolean onUpdateManufacturerData(gpointer ud);

    bool checkFactoryMode();
    static uint32_t getApIpAddr();
    static uint32_t getFirmwareVersion();
};

} // namespace ft
