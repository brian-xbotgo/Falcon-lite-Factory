#include "ble/BleAdvertiser.h"
#include "ble/BleConstants.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <mosquitto.h>
#include <arpa/inet.h>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

namespace {

constexpr const char* kDefaultBluetoothInitCmd = "/oem/usr/scripts/factory_start.sh bluetooth";

bool isValidSnText(const std::string& sn)
{
    if (sn.size() != SN_LEN) {
        return false;
    }

    bool hasNonZero = false;
    for (unsigned char ch : sn) {
        if (ch == 0 || ch == 0xff || !std::isalnum(ch)) {
            return false;
        }
        if (ch != '0') {
            hasNonZero = true;
        }
    }
    return hasNonZero;
}

std::string snFromBridge(BleAdvertiser* self)
{
    uint8_t sn[SN_LEN] = {};
    self->m_mqtt.getSn(sn);
    std::string text(reinterpret_cast<const char*>(sn), SN_LEN);
    return isValidSnText(text) ? text : std::string();
}

std::string loadLastValidSnFromFile()
{
    std::ifstream in(SN_FILE);
    std::string line;
    std::string lastValid;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() > SN_LEN) {
            line.resize(SN_LEN);
        }
        if (isValidSnText(line)) {
            lastValid = line;
        }
    }
    return lastValid;
}

std::string currentFactorySn(BleAdvertiser* self)
{
    auto sn = snFromBridge(self);
    if (!sn.empty()) {
        return sn;
    }
    return loadLastValidSnFromFile();
}

std::string currentBleName(BleAdvertiser* self)
{
    return self->m_deviceInfo.getBleName();
}

std::string factorySnForLog(BleAdvertiser* self)
{
    const auto sn = currentFactorySn(self);
    return isValidSnText(sn) ? sn : std::string("<invalid>");
}

bool bluezAdapterReady(GDBusConnection* conn, std::string& detail)
{
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        conn,
        BLUEZ_SERVICE,
        ADAPTER_PATH,
        "org.freedesktop.DBus.Introspectable",
        "Introspect",
        nullptr,
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &err);

    if (!reply) {
        detail = err ? err->message : "introspection failed";
        g_clear_error(&err);
        return false;
    }

    const gchar* xml = nullptr;
    g_variant_get(reply, "(&s)", &xml);
    const bool hasAdapter = xml && std::strstr(xml, "org.bluez.Adapter1");
    const bool hasGatt = xml && std::strstr(xml, GATT_MGR_IFACE);
    const bool hasAdv = xml && std::strstr(xml, ADV_MGR_IFACE);
    g_variant_unref(reply);

    detail.clear();
    if (!hasAdapter) detail += " Adapter1";
    if (!hasGatt) detail += " GattManager1";
    if (!hasAdv) detail += " LEAdvertisingManager1";
    return hasAdapter && hasGatt && hasAdv;
}

bool runBluetoothInitHelperOnce()
{
    static bool attempted = false;
    if (attempted) {
        return false;
    }
    attempted = true;

    const char* cmd = std::getenv("FACTORY_BLE_INIT_CMD");
    if (cmd && (cmd[0] == '\0' || std::strcmp(cmd, "0") == 0)) {
        LOG("Bluetooth init helper disabled by FACTORY_BLE_INIT_CMD\n");
        return false;
    }
    if (!cmd) {
        cmd = kDefaultBluetoothInitCmd;
    }

    LOG("BlueZ not ready; running bluetooth init helper: %s\n", cmd);
    const int rc = std::system(cmd);
    LOG("Bluetooth init helper exited rc=%d\n", rc);
    return rc == 0;
}

bool waitForBluezAdapterReady(GDBusConnection* conn)
{
    constexpr int kMaxAttempts = 120;
    constexpr int kDelayMs = 250;

    std::string detail;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        if (bluezAdapterReady(conn, detail)) {
            LOG("BlueZ adapter ready: %s has Adapter/GATT/Advertising managers\n",
                ADAPTER_PATH);
            return true;
        }

        if (attempt == 1) {
            runBluetoothInitHelperOnce();
        }

        if (attempt == 1 || attempt % 4 == 0) {
            LOG("Waiting for BlueZ adapter managers on %s, missing:%s\n",
                ADAPTER_PATH, detail.empty() ? " <unknown>" : detail.c_str());
        }
        g_usleep(kDelayMs * 1000);
    }

    LOG("BlueZ adapter managers not ready on %s after %d ms, last missing:%s\n",
        ADAPTER_PATH, kMaxAttempts * kDelayMs,
        detail.empty() ? " <unknown>" : detail.c_str());
    return false;
}

} // namespace

// ======================== D-Bus XML for LEAdvertisement1 ========================
static const gchar* g_adv_xml =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='LocalName' type='s' access='read'/>"
    "    <property name='ManufacturerData' type='a{qv}' access='read'/>"
    "    <property name='Discoverable' type='b' access='read'/>"
    "    <property name='MinInterval' type='u' access='read'/>"
    "    <property name='MaxInterval' type='u' access='read'/>"
    "  </interface>"
    "</node>";

static GVariant* advGetProperty(GDBusConnection*, const gchar*, const gchar*,
                                 const gchar*, const gchar* name, GError**, gpointer ud)
{
    auto* self = static_cast<BleAdvertiser*>(ud);

    if (!g_strcmp0(name, "Type"))           return g_variant_new_string("peripheral");
    if (!g_strcmp0(name, "Discoverable"))   return g_variant_new_boolean(true);
    if (!g_strcmp0(name, "MinInterval"))    return g_variant_new_uint32(ADV_MIN_INTERVAL);
    if (!g_strcmp0(name, "MaxInterval"))    return g_variant_new_uint32(ADV_MAX_INTERVAL);

    if (!g_strcmp0(name, "ServiceUUIDs")) {
        // In factory mode, skip ServiceUUID to save ADV space for ManufacturerData
        if (self->m_factoryMode)
            return g_variant_new_strv(nullptr, 0);
        const gchar* uuids[] = {SERVICE_UUID, nullptr};
        return g_variant_new_strv(uuids, 1);
    }

    if (!g_strcmp0(name, "LocalName")) {
        const auto bleName = currentBleName(self);
        return g_variant_new_string(bleName.c_str());
    }

    if (!g_strcmp0(name, "ManufacturerData")) {
        auto data = self->buildManufacturerData();
        if (data.empty())
            return g_variant_new_array(G_VARIANT_TYPE("{qv}"), nullptr, 0);

        GVariantBuilder ab;
        g_variant_builder_init(&ab, G_VARIANT_TYPE("ay"));
        for (size_t i = 0; i < data.size(); i++)
            g_variant_builder_add(&ab, "y", data[i]);

        GVariantBuilder mb;
        g_variant_builder_init(&mb, G_VARIANT_TYPE("a{qv}"));
        g_variant_builder_add(&mb, "{qv}", (guint16)MANUFACTURER_ID,
                              g_variant_builder_end(&ab));
        return g_variant_builder_end(&mb);
    }

    return nullptr;
}

static void advRelease(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                        const gchar*, GVariant*, GDBusMethodInvocation* inv, gpointer)
{
    g_dbus_method_invocation_return_value(inv, nullptr);
}

static GDBusInterfaceVTable g_adv_vtable = { advRelease, advGetProperty, nullptr, {} };

// ======================== Lifecycle ========================
BleAdvertiser::~BleAdvertiser()
{
    shutdown();
}

bool BleAdvertiser::checkFactoryMode()
{
    return (access(FACTORY_FLAG, F_OK) == 0);
}

void BleAdvertiser::shutdown()
{
    if (m_loop) {
        g_main_loop_quit(m_loop);
    }
}

int BleAdvertiser::setupDbus()
{
    auto connect = [this](GError** err) {
        m_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, err);
        return m_conn != nullptr;
    };

    GError* err = nullptr;
    if (connect(&err)) {
        return 0;
    }

    LOG("D-Bus connection failed: %s\n", err ? err->message : "unknown error");
    g_clear_error(&err);
    runBluetoothInitHelperOnce();

    if (connect(&err)) {
        return 0;
    }

    if (!m_conn) {
        LOG("D-Bus connection failed after bluetooth init: %s\n",
            err ? err->message : "unknown error");
        g_clear_error(&err);
        return -1;
    }
    return 0;
}

int BleAdvertiser::run()
{
    // Factory test firmware always runs in factory mode
    m_factoryMode = true;
    LOG("BLE WiFi Config starting (factory mode)\n");

    // 1. Init BLE name source
    if (!m_factoryMode) m_deviceInfo.loadDeviceAlias();

    // 2. Init MQTT. In factory mode the tester sends a 14-byte SN first; only
    // then do we register the BLE advertisement so scanners see valid SN data.
    if (m_mqtt.init(m_factoryMode, true) != 0) {
        LOG("MQTT init failed\n");
        return -1;
    }
    const std::string bleName = currentBleName(this);
    LOG("BLE name: %s\n", bleName.c_str());
    if (m_factoryMode) {
        const auto sn = currentFactorySn(this);
        if (isValidSnText(sn)) {
            std::memcpy(m_cacheSn, sn.data(), SN_LEN);
        }
    }

    // Set up MQTT → BLE bridge callbacks
    m_mqtt.setPhoneConnectHandler([this](bool connected) {
        if (connected) stopAdvertisement();
        else startAdvertisement();
    });

    // 3. Setup D-Bus
    auto failAfterMqtt = [this]() {
        if (m_conn) {
            g_object_unref(m_conn);
            m_conn = nullptr;
        }
        m_mqtt.deinit();
        return -1;
    };

    if (setupDbus() != 0) return failAfterMqtt();
    if (!waitForBluezAdapterReady(m_conn)) return failAfterMqtt();

    // 4. Wire up GattServer collaborators
    m_gatt.setWifiManager(&m_wifi);
    m_gatt.setMqttBridge(&m_mqtt);
    m_gatt.setFactoryMode(m_factoryMode);
    m_gatt.setStopAdvCb([this]() { stopAdvertisement(); });
    m_gatt.setStartAdvCb([this]() { startAdvertisement(); });

    // 5. Create and register GATT objects
    m_gatt.create(m_conn);

    // 6. Create and register advertisement
    createAdvertisement();
    registerAdvertisement();

    // 7. Register GATT with BlueZ
    m_gatt.setBleName(m_conn, bleName.c_str());
    m_gatt.registerWithBlueZ(m_conn);

    // 8. Register WiFi state callback
    m_wifi.registerStateCallback(BleGattServer::onWifiStateChanged);

    // 9. Start 1-second timer for manufacturer data updates
    g_timeout_add(1000, onUpdateManufacturerData, this);

    // 10. Enter main loop
    m_loop = g_main_loop_new(nullptr, false);
    LOG("BLE WiFi Config service started\n");
    g_main_loop_run(m_loop);

    // Cleanup
    g_main_loop_unref(m_loop);
    m_loop = nullptr;
    if (m_conn) { g_object_unref(m_conn); m_conn = nullptr; }
    m_mqtt.deinit();
    return 0;
}

// ======================== Advertisement ========================
void BleAdvertiser::createAdvertisement()
{
    GError* err = nullptr;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(g_adv_xml, &err);
    if (!node) { LOG("Adv XML: %s\n", err->message); g_error_free(err); return; }

    g_dbus_connection_register_object(m_conn, ADV_OBJ_PATH,
                                      node->interfaces[0], &g_adv_vtable,
                                      this, nullptr, &err);
    if (err) { LOG("Adv obj: %s\n", err->message); g_clear_error(&err); }
}

void BleAdvertiser::registerAdvertisement()
{
    if (m_factoryMode) {
        const auto bleName = currentBleName(this);
        const auto sn = factorySnForLog(this);
        LOG("Factory BLE advertisement payload: name=%s manufacturer_sn=%s\n",
            bleName.c_str(), sn.c_str());
    }

    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&opts, "{sv}", "param", g_variant_new_string("value"));

    g_dbus_connection_call(m_conn, BLUEZ_SERVICE, ADAPTER_PATH,
        ADV_MGR_IFACE, "RegisterAdvertisement",
        g_variant_new("(o@a{sv})", ADV_OBJ_PATH, g_variant_builder_end(&opts)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject*, GAsyncResult* res, gpointer ud) {
            auto* self = static_cast<BleAdvertiser*>(ud);
            GError* err = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(self->m_conn, res, &err);
            if (!reply) { LOG("RegisterAdvertisement: %s\n", err->message); g_error_free(err); return; }
            g_variant_unref(reply);
            self->m_advRegistered = true;
            self->m_isAdvertising = true;
            LOG("BLE advertisement registered\n");
        }, this);
}

void BleAdvertiser::startAdvertisement()
{
    if (m_isAdvertising || !m_conn) return;

    if (m_factoryMode) {
        const auto bleName = currentBleName(this);
        const auto sn = factorySnForLog(this);
        LOG("Factory BLE advertisement restart: name=%s manufacturer_sn=%s\n",
            bleName.c_str(), sn.c_str());
    }

    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&opts, "{sv}", "param", g_variant_new_string("value"));

    g_dbus_connection_call(m_conn, BLUEZ_SERVICE, ADAPTER_PATH,
        ADV_MGR_IFACE, "RegisterAdvertisement",
        g_variant_new("(o@a{sv})", ADV_OBJ_PATH, g_variant_builder_end(&opts)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject*, GAsyncResult* res, gpointer ud) {
            auto* self = static_cast<BleAdvertiser*>(ud);
            GError* err = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(self->m_conn, res, &err);
            if (!reply) { LOG("startAdvertisement: %s\n", err->message); g_error_free(err); return; }
            g_variant_unref(reply);
            self->m_isAdvertising = true;
            LOG("Advertisement restarted\n");
        }, this);
}

void BleAdvertiser::stopAdvertisement()
{
    if (!m_isAdvertising || !m_conn) return;

    GVariant* result = g_dbus_connection_call_sync(m_conn, BLUEZ_SERVICE, ADAPTER_PATH,
        ADV_MGR_IFACE, "UnregisterAdvertisement",
        g_variant_new("(o)", ADV_OBJ_PATH), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    if (result) g_variant_unref(result);
    m_isAdvertising = false;
    LOG("Advertisement stopped\n");
}

// ======================== Manufacturer Data ========================
uint32_t BleAdvertiser::getApIpAddr()
{
    // Parse 192.168.5.1 into uint32_t
    uint32_t ip;
    inet_pton(AF_INET, HOSTAP_IP, &ip);
    return ip;
}

uint32_t BleAdvertiser::getFirmwareVersion()
{
    // TODO: Read from version file or embed at build time
    return 0x01000000;  // version 1.0.0.0
}

std::vector<uint8_t> BleAdvertiser::buildManufacturerData()
{
    if (m_factoryMode) {
        const auto sn = currentFactorySn(this);
        if (!isValidSnText(sn)) {
            return {};
        }
        return std::vector<uint8_t>(sn.begin(), sn.end());
    }

    // Normal mode: STA_COUNT(4B) + IP(4B) + VERSION(4B) + COLOR(1B) + LIVE(1B) + ALIAS_LEN(1B) + ALIAS(N)
    int staCount = m_wifi.getStaCount(AP_INTERFACE);
    if (staCount < 0) staCount = 0;

    uint32_t ip = getApIpAddr();
    uint32_t ver = getFirmwareVersion();
    uint8_t color = (uint8_t)m_deviceInfo.getDeviceColor();
    uint8_t liveStatus = (uint8_t)m_mqtt.getLiveStatus();
    const char* alias = m_deviceInfo.getDeviceAlias();
    size_t aliasLen = strlen(alias);
    if (aliasLen > MAX_ALIAS_LEN) aliasLen = MAX_ALIAS_LEN;

    std::vector<uint8_t> data;
    // STA count (4 bytes, big-endian)
    data.push_back((staCount >> 24) & 0xFF);
    data.push_back((staCount >> 16) & 0xFF);
    data.push_back((staCount >> 8) & 0xFF);
    data.push_back(staCount & 0xFF);
    // IP (4 bytes, big-endian)
    data.push_back((ip >> 24) & 0xFF);
    data.push_back((ip >> 16) & 0xFF);
    data.push_back((ip >> 8) & 0xFF);
    data.push_back(ip & 0xFF);
    // Version (4 bytes, big-endian)
    data.push_back((ver >> 24) & 0xFF);
    data.push_back((ver >> 16) & 0xFF);
    data.push_back((ver >> 8) & 0xFF);
    data.push_back(ver & 0xFF);
    // Color (1 byte)
    data.push_back(color);
    // Live status (1 byte)
    data.push_back(liveStatus);
    // Alias length (1 byte) + alias (N bytes)
    data.push_back((uint8_t)aliasLen);
    for (size_t i = 0; i < aliasLen; i++)
        data.push_back((uint8_t)alias[i]);

    return data;
}

gboolean BleAdvertiser::onUpdateManufacturerData(gpointer ud)
{
    auto* self = static_cast<BleAdvertiser*>(ud);
    if (!self->m_conn || !self->m_isAdvertising) return G_SOURCE_CONTINUE;

    // In factory mode, skip if SN not valid
    if (self->m_factoryMode && !self->m_mqtt.hasValidSn()) return G_SOURCE_CONTINUE;

    bool changed = false;

    if (self->m_factoryMode) {
        // Compare SN against cache, skip PropertiesChanged if unchanged
        const auto sn = currentFactorySn(self);
        if (!isValidSnText(sn)) return G_SOURCE_CONTINUE;
        if (memcmp(sn.data(), self->m_cacheSn, SN_LEN) == 0) return G_SOURCE_CONTINUE;
        memcpy(self->m_cacheSn, sn.data(), SN_LEN);
        changed = true;
    } else {
        int staCount = self->m_wifi.getStaCount(AP_INTERFACE);
        if (staCount < 0) staCount = 0;

        uint32_t ip = getApIpAddr();
        uint8_t liveStatus = (uint8_t)self->m_mqtt.getLiveStatus();
        uint8_t color = (uint8_t)self->m_deviceInfo.getDeviceColor();
        const char* alias = self->m_deviceInfo.getDeviceAlias();

        if ((uint32_t)staCount != self->m_cacheStaCount) { self->m_cacheStaCount = staCount; changed = true; }
        if (ip != self->m_cacheIp) { self->m_cacheIp = ip; changed = true; }
        if (liveStatus != self->m_cacheLiveStatus) { self->m_cacheLiveStatus = liveStatus; changed = true; }
        if (color != self->m_cacheColor) { self->m_cacheColor = color; changed = true; }
        if (strcmp(alias, self->m_cacheAlias) != 0) {
            strncpy(self->m_cacheAlias, alias, sizeof(self->m_cacheAlias) - 1);
            changed = true;
        }
        if (!changed) return G_SOURCE_CONTINUE;
    }

    // Build and emit PropertiesChanged
    auto data = self->buildManufacturerData();

    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

    const auto bleName = currentBleName(self);
    g_variant_builder_add(&props, "{sv}", "LocalName",
                          g_variant_new_string(bleName.c_str()));

    // ManufacturerData
    GVariantBuilder ab;
    g_variant_builder_init(&ab, G_VARIANT_TYPE("ay"));
    for (size_t i = 0; i < data.size(); i++)
        g_variant_builder_add(&ab, "y", data[i]);

    GVariantBuilder mb;
    g_variant_builder_init(&mb, G_VARIANT_TYPE("a{qv}"));
    g_variant_builder_add(&mb, "{qv}", (guint16)MANUFACTURER_ID,
                          g_variant_builder_end(&ab));
    g_variant_builder_add(&props, "{sv}", "ManufacturerData",
                          g_variant_builder_end(&mb));

    GVariant* invalidated = g_variant_new_strv(nullptr, 0);

    g_dbus_connection_emit_signal(self->m_conn, nullptr, ADV_OBJ_PATH,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(s@a{sv}@as)", "org.bluez.LEAdvertisement1",
                      g_variant_builder_end(&props), invalidated), nullptr);

    if (self->m_factoryMode && changed) {
        self->m_gatt.setBleName(self->m_conn, bleName.c_str());
        self->stopAdvertisement();
        self->startAdvertisement();
        LOG("Factory SN changed, BLE advertisement refreshed name=%s\n", bleName.c_str());
    }

    return G_SOURCE_CONTINUE;
}

} // namespace ft
