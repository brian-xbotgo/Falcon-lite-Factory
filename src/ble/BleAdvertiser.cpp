#include "ble/BleAdvertiser.h"
#include "ble/BleConstants.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <mosquitto.h>
#include <arpa/inet.h>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

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
        return g_variant_new_string("Xbt-F-000000");
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
    GError* err = nullptr;
    m_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (!m_conn) {
        LOG("D-Bus connection failed: %s\n", err->message);
        g_error_free(err);
        return -1;
    }
    return 0;
}

int BleAdvertiser::run()
{
    // Factory test firmware always runs in factory mode
    m_factoryMode = true;
    LOG("BLE WiFi Config starting (factory mode)\n");

    // 1. Init BLE name (fixed "Xbt-F-" prefix for phone app discovery)
    const char* bleName = "Xbt-F-000000";
    LOG("BLE name: %s\n", bleName);
    if (!m_factoryMode) m_deviceInfo.loadDeviceAlias();

    // 2. Init MQTT (block until SN received in factory mode)
    // In factory mode, wait for SN through MQTT
    if (m_mqtt.init(m_factoryMode, m_factoryMode) != 0) {
        LOG("MQTT init failed\n");
        return -1;
    }

    // Set up MQTT → BLE bridge callbacks
    m_mqtt.setPhoneConnectHandler([this](bool connected) {
        if (connected) stopAdvertisement();
        else startAdvertisement();
    });

    // 3. Setup D-Bus
    if (setupDbus() != 0) return -1;

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
    m_gatt.setBleName(m_conn, bleName);
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
        uint8_t sn[SN_LEN] = {};
        m_mqtt.getSn(sn);
        // Check if SN is all zeros
        bool allZero = true;
        for (int i = 0; i < SN_LEN; i++) { if (sn[i] != 0) { allZero = false; break; } }
        if (allZero) {
            // Try reading SN from file
            FILE* f = fopen(SN_FILE, "r");
            if (f) {
                char buf[32] = {};
                if (fgets(buf, sizeof(buf), f)) {
                    size_t len = strlen(buf);
                    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
                    if (len >= SN_LEN) memcpy(sn, buf, SN_LEN);
                }
                fclose(f);
            }
        }
        return std::vector<uint8_t>(sn, sn + SN_LEN);
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
        const uint8_t* sn = self->m_mqtt.getSnBuf();
        if (memcmp(sn, self->m_cacheSn, SN_LEN) == 0) return G_SOURCE_CONTINUE;
        memcpy(self->m_cacheSn, sn, SN_LEN);
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

    // LocalName (fixed "Xbt-F-" prefix for phone app discovery)
    g_variant_builder_add(&props, "{sv}", "LocalName",
                          g_variant_new_string("Xbt-F-000000"));

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

    return G_SOURCE_CONTINUE;
}

} // namespace ft
