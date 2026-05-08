#pragma once

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include "ble/BleConstants.h"

namespace ft {

class BleWifiManager;
class BleMqttBridge;

class BleGattServer {
public:
    BleGattServer() = default;
    ~BleGattServer() = default;

    // Non-copyable
    BleGattServer(const BleGattServer&) = delete;
    BleGattServer& operator=(const BleGattServer&) = delete;

    // Set collaborators (not owned).
    void setWifiManager(BleWifiManager* wifi) { m_wifi = wifi; }
    void setMqttBridge(BleMqttBridge* mqtt) { m_mqtt = mqtt; }

    // Callbacks for advertisement control (called when BLE central connects/disconnects).
    void setStopAdvCb(std::function<void()> cb) { m_stopAdvCb = std::move(cb); }
    void setStartAdvCb(std::function<void()> cb) { m_startAdvCb = std::move(cb); }

    // In factory mode, skip connect/disconnect signal handling (ad stays on).
    void setFactoryMode(bool factory) { m_factoryMode = factory; }

    // Register all D-Bus objects (ObjectManager + Service + 8 Characteristics).
    void create(GDBusConnection* conn);

    // Call GattManager1.RegisterApplication.
    void registerWithBlueZ(GDBusConnection* conn);

    // Set BLE adapter alias via D-Bus.
    void setBleName(GDBusConnection* conn, const char* name);

    // Send PropertiesChanged notification for a characteristic.
    void notifyChar(GDBusConnection* conn, int idx, const uint8_t* data, int len);

    // WiFi state change handler — spawns thread to send status notification.
    static int onWifiStateChanged(uint8_t state, void* info);

    // Expose for the global WiFi callback (needs static access to instance).
    static BleGattServer* s_instance;

private:
    BleWifiManager* m_wifi = nullptr;
    BleMqttBridge*  m_mqtt = nullptr;

    std::function<void()> m_stopAdvCb;
    std::function<void()> m_startAdvCb;
    bool m_factoryMode = false;

    // Characteristic storage
    uint8_t m_charVals[CHAR_COUNT][MAX_CHAR_VAL] = {};
    int     m_charLens[CHAR_COUNT] = {};
    bool    m_notifying[CHAR_COUNT] = {};

    // WiFi connection status
    int m_wifiStatus = WIFI_State_IDLE;

    // ======================== D-Bus callbacks ========================
    static GVariant* onGetProperty(GDBusConnection*, const gchar*, const gchar* path,
                                   const gchar*, const gchar* name, GError**, gpointer ud);
    static void onMethodCall(GDBusConnection*, const gchar*, const gchar* path,
                             const gchar*, const gchar* method, GVariant* params,
                             GDBusMethodInvocation* inv, gpointer ud);
    static void onGetManagedObjects(GDBusConnection*, const gchar*, const gchar*,
                                    const gchar*, const gchar*, GVariant*,
                                    GDBusMethodInvocation* inv, gpointer);

    // Property helpers
    GVariant* getCharProperty(const gchar* path, const gchar* name);

    // ======================== BLE central connect/disconnect ========================
    static void onInterfacesAdded(GDBusConnection*, const gchar*, const gchar*,
                                  const gchar*, const gchar*, GVariant* params, gpointer ud);
    static void onInterfacesRemoved(GDBusConnection*, const gchar*, const gchar*,
                                    const gchar*, const gchar*, GVariant* params, gpointer ud);
    void handleDeviceConnected();
    void handleDeviceDisconnected();

    // ======================== OP dispatch ========================
    void opParse(const uint8_t* data, int len);

    // Thread functions
    static void* sendWifiCfgThread(void* arg);
    static void* sendWifiListThread(void* arg);
    static void* sendWifiIpThread(void* arg);
    static void* sendWifiStatusThread(void* arg);
    static void* connectHotspotThread(void* arg);
    static void* disconnectHotspotThread(void* arg);
    static void* sendLiveStatusThread(void* arg);
};

} // namespace ft
