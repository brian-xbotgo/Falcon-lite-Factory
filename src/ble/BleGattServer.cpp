#include "ble/BleGattServer.h"
#include "ble/BleWifiManager.h"
#include "ble/BleMqttBridge.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <thread>
#include <chrono>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

// Path constants (must be macros for string-literal concatenation in C++)
#define APP_ROOT_PATH   "/org/bluez/example/server"
#define SVC_PATH        "/org/bluez/example/server/service00"
#define CHAR_PREFIX     "/org/bluez/example/server/service00/char000"

namespace ft {

BleGattServer* BleGattServer::s_instance = nullptr;

// ======================== D-Bus XML introspection data ========================
static const gchar* g_object_manager_xml =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "    <signal name='InterfacesAdded'>"
    "      <arg name='object' type='o'/>"
    "      <arg name='interfaces' type='a{sa{sv}}'/>"
    "    </signal>"
    "    <signal name='InterfacesRemoved'>"
    "      <arg name='object' type='o'/>"
    "      <arg name='interfaces' type='as'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static const gchar* g_service_xml =
    "<node>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar* g_char_xml =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "    <property name='Notifying' type='b' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <method name='ReadValue'>"
    "      <arg name='value' type='ay' direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg name='value' type='ay' direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "  </interface>"
    "</node>";

// Characteristic flags matching bleConfigureWifi FF01-FF08
static const char* kCharFlags[8][4] = {
    {"write", "write-without-response", nullptr},     // FF01 op_info_in
    {"notify", nullptr},                                // FF02 ap_info_out
    {"write", "write-without-response", nullptr},       // FF03 hotspot_info_in
    {"notify", nullptr},                                // FF04 wifi_list_out
    {"notify", nullptr},                                // FF05 wifi_status_out
    {"notify", nullptr},                                // FF06 wifi_ip_out
    {"notify", nullptr},                                // FF07 live_status_out
    {"write", "write-without-response", nullptr},       // FF08 app_token_in
};

// ======================== Path helpers ========================
static int charIndexFromPath(const gchar* path)
{
    // Path format: /org/bluez/example/server/service00/char0000
    const char* prefix = CHAR_PREFIX;
    size_t plen = strlen(prefix);
    if (g_str_has_prefix(path, prefix) && strlen(path) == plen + 1) {
        int idx = path[plen] - '0';
        if (idx >= 0 && idx < CHAR_COUNT) return idx;
    }
    return -1;
}

static const char* charPath(int idx)
{
    static char buf[256];
    snprintf(buf, sizeof(buf), SVC_PATH "/char000%d", idx);
    return buf;
}

// ======================== D-Bus property getter ========================
GVariant* BleGattServer::getCharProperty(const gchar* path, const gchar* name)
{
    // Service properties
    if (g_strcmp0(path, SVC_PATH) == 0) {
        if (!g_strcmp0(name, "UUID"))    return g_variant_new_string(SERVICE_UUID);
        if (!g_strcmp0(name, "Primary")) return g_variant_new_boolean(true);
        return nullptr;
    }

    int idx = charIndexFromPath(path);
    if (idx < 0) return nullptr;

    if (!g_strcmp0(name, "UUID"))
        return g_variant_new_string(kCharUuids[idx]);
    if (!g_strcmp0(name, "Service"))
        return g_variant_new_object_path(SVC_PATH);
    if (!g_strcmp0(name, "Flags"))
        return g_variant_new_strv(kCharFlags[idx], -1);
    if (!g_strcmp0(name, "Notifying"))
        return g_variant_new_boolean(m_notifying[idx]);
    if (!g_strcmp0(name, "Value")) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ay"));
        for (int i = 0; i < m_charLens[idx]; i++)
            g_variant_builder_add(&b, "y", m_charVals[idx][i]);
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

GVariant* BleGattServer::onGetProperty(GDBusConnection*, const gchar*, const gchar* path,
                                        const gchar*, const gchar* name, GError**, gpointer ud)
{
    auto* self = static_cast<BleGattServer*>(ud);
    return self->getCharProperty(path, name);
}

// ======================== D-Bus method call dispatch ========================
void BleGattServer::onMethodCall(GDBusConnection*, const gchar*, const gchar* path,
                                  const gchar*, const gchar* method, GVariant* params,
                                  GDBusMethodInvocation* inv, gpointer ud)
{
    auto* self = static_cast<BleGattServer*>(ud);

    // GetManagedObjects on the app root
    if (g_strcmp0(path, APP_ROOT_PATH) == 0) {
        if (g_strcmp0(method, "GetManagedObjects") == 0) {
            onGetManagedObjects(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, inv, ud);
            return;
        }
    }

    int idx = charIndexFromPath(path);
    if (idx < 0 || idx >= CHAR_COUNT) {
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (g_strcmp0(method, "ReadValue") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ay"));
        for (int i = 0; i < self->m_charLens[idx]; i++)
            g_variant_builder_add(&b, "y", self->m_charVals[idx][i]);
        GVariant* v = g_variant_builder_end(&b);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", v));
        return;
    }

    if (g_strcmp0(method, "WriteValue") == 0) {
        GVariant* val = g_variant_get_child_value(params, 0);
        gsize n;
        const guint8* data = (const guint8*)g_variant_get_fixed_array(val, &n, 1);
        if (n > 0 && n < MAX_CHAR_VAL) {
            memcpy(self->m_charVals[idx], data, n);
            self->m_charLens[idx] = n;
        }
        g_variant_unref(val);
        g_dbus_method_invocation_return_value(inv, nullptr);

        // Dispatch based on characteristic index
        switch (idx) {
        case CHAR_OP_INFO:       // FF01 — operation command
            self->opParse(data, (int)n);
            break;
        case CHAR_HOTSPOT_INFO:  // FF03 — WiFi credentials → connect
            if (n > 0) {
                auto* arg = new std::pair<BleGattServer*, std::vector<uint8_t>>(
                    self, std::vector<uint8_t>(data, data + n));
                pthread_t tid;
                pthread_create(&tid, nullptr, connectHotspotThread, arg);
                pthread_detach(tid);
            }
            break;
        case CHAR_APP_TOKEN:     // FF08 — app token
            if (self->m_mqtt && n > 0) {
                char token[MAX_CHAR_VAL + 1] = {};
                memcpy(token, data, n);
                self->m_mqtt->publishAppToken(token);
            }
            break;
        }
        return;
    }

    if (g_strcmp0(method, "StartNotify") == 0) {
        self->m_notifying[idx] = true;
        LOG("GATT StartNotify char%d\n", idx);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (g_strcmp0(method, "StopNotify") == 0) {
        self->m_notifying[idx] = false;
        LOG("GATT StopNotify char%d\n", idx);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    g_dbus_method_invocation_return_value(inv, nullptr);
}

// ======================== GetManagedObjects ========================
void BleGattServer::onGetManagedObjects(GDBusConnection*, const gchar*, const gchar*,
                                         const gchar*, const gchar*, GVariant*,
                                         GDBusMethodInvocation* inv, gpointer)
{
    GVariantBuilder root;
    g_variant_builder_init(&root, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

    // Service properties
    GVariantBuilder sp;
    g_variant_builder_init(&sp, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&sp, "{sv}", "UUID", g_variant_new_string(SERVICE_UUID));
    g_variant_builder_add(&sp, "{sv}", "Primary", g_variant_new_boolean(true));

    GVariantBuilder svcIf;
    g_variant_builder_init(&svcIf, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_add(&svcIf, "{s@a{sv}}", "org.bluez.GattService1",
                          g_variant_builder_end(&sp));
    g_variant_builder_add(&root, "{o@a{sa{sv}}}", SVC_PATH,
                          g_variant_builder_end(&svcIf));

    // All 8 characteristics
    for (int i = 0; i < CHAR_COUNT; i++) {
        GVariantBuilder cp;
        g_variant_builder_init(&cp, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&cp, "{sv}", "UUID", g_variant_new_string(kCharUuids[i]));
        g_variant_builder_add(&cp, "{sv}", "Service", g_variant_new_object_path(SVC_PATH));
        g_variant_builder_add(&cp, "{sv}", "Flags", g_variant_new_strv(kCharFlags[i], -1));

        GVariantBuilder chrIf;
        g_variant_builder_init(&chrIf, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&chrIf, "{s@a{sv}}", "org.bluez.GattCharacteristic1",
                              g_variant_builder_end(&cp));
        g_variant_builder_add(&root, "{o@a{sa{sv}}}", charPath(i),
                              g_variant_builder_end(&chrIf));
    }

    GVariant* r = g_variant_builder_end(&root);
    g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})", r));
}

// ======================== Create / Register GATT objects ========================
void BleGattServer::create(GDBusConnection* conn)
{
    s_instance = this;
    GError* err = nullptr;

    // ObjectManager on APP_ROOT_PATH
    GDBusNodeInfo* omNode = g_dbus_node_info_new_for_xml(g_object_manager_xml, &err);
    if (!omNode) { LOG("OM XML: %s\n", err->message); g_clear_error(&err); }
    else {
        static GDBusInterfaceVTable omVtable = { onGetManagedObjects, nullptr, nullptr };
        g_dbus_connection_register_object(conn, APP_ROOT_PATH, omNode->interfaces[0],
                                          &omVtable, this, nullptr, &err);
        if (err) { LOG("OM obj: %s\n", err->message); g_clear_error(&err); }
    }

    // Service
    GDBusNodeInfo* svcNode = g_dbus_node_info_new_for_xml(g_service_xml, &err);
    if (!svcNode) { LOG("Svc XML: %s\n", err->message); g_clear_error(&err); }
    else {
        static GDBusInterfaceVTable svcVtable = { nullptr, onGetProperty, nullptr, {} };
        g_dbus_connection_register_object(conn, SVC_PATH,
                                          svcNode->interfaces[0], &svcVtable, this, nullptr, &err);
        if (err) { LOG("Svc obj: %s\n", err->message); g_clear_error(&err); }
    }

    // 8 Characteristics
    GDBusNodeInfo* chrNode = g_dbus_node_info_new_for_xml(g_char_xml, &err);
    if (!chrNode) { LOG("Chr XML: %s\n", err->message); g_clear_error(&err); return; }

    static GDBusInterfaceVTable chrVtable = { onMethodCall, onGetProperty, nullptr, {} };
    for (int i = 0; i < CHAR_COUNT; i++) {
        g_dbus_connection_register_object(conn, charPath(i),
                                          chrNode->interfaces[0], &chrVtable, this, nullptr, &err);
        if (err) { LOG("Char%d obj: %s\n", i, err->message); g_clear_error(&err); }
    }

    // Register for BLE central connect/disconnect signals (skip in factory mode)
    if (!m_factoryMode) {
        g_dbus_connection_signal_subscribe(conn, BLUEZ_SERVICE, "org.freedesktop.DBus.ObjectManager",
                                           "InterfacesAdded",   nullptr, nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                           onInterfacesAdded, this, nullptr);
        g_dbus_connection_signal_subscribe(conn, BLUEZ_SERVICE, "org.freedesktop.DBus.ObjectManager",
                                           "InterfacesRemoved", nullptr, nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                           onInterfacesRemoved, this, nullptr);
    }
}

void BleGattServer::setBleName(GDBusConnection* conn, const char* name)
{
    if (!conn || !name) return;
    GError* err = nullptr;
    GVariant* result = g_dbus_connection_call_sync(conn, BLUEZ_SERVICE, ADAPTER_PATH,
        "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", "Alias",
                      g_variant_new_string(name)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
    if (err) { LOG("Set Alias: %s\n", err->message); g_clear_error(&err); }
    else if (result) g_variant_unref(result);
}

void BleGattServer::registerWithBlueZ(GDBusConnection* conn)
{
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&opts, "{sv}", "param", g_variant_new_string("value"));

    g_dbus_connection_call(conn, BLUEZ_SERVICE, ADAPTER_PATH,
        GATT_MGR_IFACE, "RegisterApplication",
        g_variant_new("(o@a{sv})", APP_ROOT_PATH, g_variant_builder_end(&opts)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer) {
            GError* e = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
            if (!e) { if (reply) g_variant_unref(reply); LOG("GATT server registered\n"); }
            else { LOG("RegisterApplication: %s\n", e->message); g_error_free(e); }
        }, nullptr);
}

// ======================== Notify characteristic ========================
void BleGattServer::notifyChar(GDBusConnection* conn, int idx, const uint8_t* data, int len)
{
    if (!conn || idx < 0 || idx >= CHAR_COUNT || !m_notifying[idx]) return;

    // Update stored value
    len = std::min(len, MAX_CHAR_VAL);
    memcpy(m_charVals[idx], data, len);
    m_charLens[idx] = len;

    // Build changed properties
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

    GVariantBuilder ab;
    g_variant_builder_init(&ab, G_VARIANT_TYPE("ay"));
    for (int i = 0; i < len; i++)
        g_variant_builder_add(&ab, "y", data[i]);
    g_variant_builder_add(&props, "{sv}", "Value", g_variant_builder_end(&ab));

    GVariantBuilder nb;
    g_variant_builder_init(&nb, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&nb, "{sv}", "Notifying", g_variant_new_boolean(m_notifying[idx]));
    g_variant_builder_add(&props, "{sv}", "Notifying", g_variant_new_boolean(m_notifying[idx]));

    GVariant* invalidated = g_variant_new_strv(nullptr, 0);

    g_dbus_connection_emit_signal(conn, nullptr, charPath(idx),
                                  "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                  g_variant_new("(s@a{sv}@as)",
                                      "org.bluez.GattCharacteristic1",
                                      g_variant_builder_end(&props),
                                      invalidated),
                                  nullptr);
}

// ======================== BLE central connect/disconnect ========================
void BleGattServer::onInterfacesAdded(GDBusConnection*, const gchar*, const gchar*,
                                       const gchar*, const gchar*, GVariant* params, gpointer ud)
{
    auto* self = static_cast<BleGattServer*>(ud);
    if (!params) return;
    gchar* objPath = nullptr;
    GVariant* ifaces = nullptr;
    g_variant_get(params, "(o@a{sa{sv}})", &objPath, &ifaces);
    if (objPath && strstr(objPath, "/dev_")) self->handleDeviceConnected();
    g_free(objPath);
}

void BleGattServer::onInterfacesRemoved(GDBusConnection*, const gchar*, const gchar*,
                                         const gchar*, const gchar*, GVariant* params, gpointer ud)
{
    auto* self = static_cast<BleGattServer*>(ud);
    if (!params) return;
    gchar* objPath = nullptr;
    g_variant_get(params, "(o)", &objPath);
    if (objPath && strstr(objPath, "/dev_")) self->handleDeviceDisconnected();
    g_free(objPath);
}

void BleGattServer::handleDeviceConnected()
{
    LOG("BLE central connected, stopping advertisement\n");
    if (m_stopAdvCb) m_stopAdvCb();
}

void BleGattServer::handleDeviceDisconnected()
{
    LOG("BLE central disconnected, restarting advertisement\n");
    if (m_startAdvCb) m_startAdvCb();
}

// ======================== WiFi state callback ========================
int BleGattServer::onWifiStateChanged(uint8_t state, void* info)
{
    (void)info;
    auto* self = s_instance;
    if (!self) return 0;

    self->m_wifiStatus = state;

    auto* arg = new std::pair<BleGattServer*, int>(self, state);
    pthread_t tid;
    pthread_create(&tid, nullptr, sendWifiStatusThread, arg);
    pthread_detach(tid);
    return 0;
}

// ======================== OP dispatch ========================
void BleGattServer::opParse(const uint8_t* data, int len)
{
    if (len < 1) return;
    uint8_t op = data[0];

    switch (op) {
    case OP_GET_AP_INFO: {
        auto* arg = new BleGattServer*(this);
        pthread_t tid;
        pthread_create(&tid, nullptr, sendWifiCfgThread, arg);
        pthread_detach(tid);
        break;
    }
    case OP_GET_WIFI_LIST: {
        auto* arg = new BleGattServer*(this);
        pthread_t tid;
        pthread_create(&tid, nullptr, sendWifiListThread, arg);
        pthread_detach(tid);
        break;
    }
    case OP_GET_WIFI_STATUS: {
        auto* arg = new std::pair<BleGattServer*, int>(this, m_wifiStatus);
        pthread_t tid;
        pthread_create(&tid, nullptr, sendWifiStatusThread, arg);
        pthread_detach(tid);
        break;
    }
    case OP_DISCONNECT_HOTSPOT: {
        auto* arg = new BleGattServer*(this);
        pthread_t tid;
        pthread_create(&tid, nullptr, disconnectHotspotThread, arg);
        pthread_detach(tid);
        break;
    }
    case OP_STOP_LIVE: {
        auto* arg = new BleGattServer*(this);
        pthread_t tid;
        pthread_create(&tid, nullptr, sendLiveStatusThread, arg);
        pthread_detach(tid);
        break;
    }
    default:
        break;
    }
}

// ======================== Thread functions ========================
void* BleGattServer::sendWifiCfgThread(void* arg)
{
    auto* self = *(BleGattServer**)arg;
    delete (BleGattServer**)arg;

    if (!self->m_wifi) return nullptr;

    int cfgLen = 0;
    auto cfg = self->m_wifi->getWifiCfg(AP_INTERFACE, cfgLen);
    if (!cfg.empty()) {
        // Notify on char0001 (ap_info_out)
        GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
        self->notifyChar(conn, CHAR_AP_INFO, cfg.data(), cfgLen);
        g_object_unref(conn);
    }
    return nullptr;
}

void* BleGattServer::sendWifiListThread(void* arg)
{
    auto* self = *(BleGattServer**)arg;
    delete (BleGattServer**)arg;

    if (!self->m_wifi) return nullptr;

    std::string json = self->m_wifi->scanWifi();
    if (json.empty()) return nullptr;

    WifiList wifiList = {};
    if (self->m_wifi->parseScanResult(json.c_str(), wifiList) != 0) return nullptr;
    if (wifiList.wifiNum == 0) return nullptr;

    WifiIterator it;
    self->m_wifi->iteratorInit(it, wifiList);

    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    char buf[BLE_MTU_LEN];
    while (self->m_wifi->iteratorNext(it, buf, sizeof(buf)) == 0) {
        self->notifyChar(conn, CHAR_WIFI_LIST, (const uint8_t*)buf, strlen(buf));
    }
    g_object_unref(conn);

    free(wifiList.wifiInfo);
    return nullptr;
}

void* BleGattServer::sendWifiIpThread(void* arg)
{
    auto* self = *(BleGattServer**)arg;
    delete (BleGattServer**)arg;

    if (!self->m_wifi) return nullptr;

    std::string ipv4 = self->m_wifi->getStaIp(STA_INTERFACE);
    std::vector<std::string> ipv6Addrs;
    self->m_wifi->getStaIpv6(STA_INTERFACE, ipv6Addrs, 4);

    if (ipv4.empty() && ipv6Addrs.empty()) return nullptr;

    char ipv6Arr[4][WIFI_IPV6_LEN] = {};
    int addrCount = 0;
    for (size_t i = 0; i < ipv6Addrs.size() && i < 4; i++) {
        strncpy(ipv6Arr[i], ipv6Addrs[i].c_str(), WIFI_IPV6_LEN - 1);
        addrCount++;
    }

    int packLen = WifiIpLength((char*)ipv4.c_str(), ipv6Arr, addrCount);
    if (packLen <= 0) return nullptr;

    std::vector<uint8_t> buf(packLen);
    if (WifiIpPack((char*)ipv4.c_str(), ipv6Arr, addrCount, (char*)buf.data()) != 0)
        return nullptr;

    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    self->notifyChar(conn, CHAR_WIFI_IP, buf.data(), packLen);
    g_object_unref(conn);
    return nullptr;
}

void* BleGattServer::sendWifiStatusThread(void* arg)
{
    auto* pair = (std::pair<BleGattServer*, int>*)arg;
    auto* self = pair->first;
    int status = pair->second;
    delete pair;

    uint8_t data = (uint8_t)status;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    self->notifyChar(conn, CHAR_WIFI_STATUS, &data, 1);

    // If DHCP_OK, also send IP
    if (status == WIFI_State_DHCP_OK) {
        auto* ipArg = new BleGattServer*(self);
        pthread_t tid;
        pthread_create(&tid, nullptr, sendWifiIpThread, ipArg);
        pthread_detach(tid);
    }
    g_object_unref(conn);
    return nullptr;
}

void* BleGattServer::connectHotspotThread(void* arg)
{
    auto* pair = (std::pair<BleGattServer*, std::vector<uint8_t>>*)arg;
    auto* self = pair->first;
    auto data = std::move(pair->second);
    delete pair;

    if (!self->m_wifi) return nullptr;

    // Clean up old scan results
    self->m_wifi->wifiListCleanup();

    int result = self->m_wifi->connectHotspot(data.data(), data.size());
    int state = (result == 0) ? WIFI_State_CONNECTING : WIFI_State_CONNECTFAILED;

    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    uint8_t st = (uint8_t)state;
    self->notifyChar(conn, CHAR_WIFI_STATUS, &st, 1);
    g_object_unref(conn);
    return nullptr;
}

void* BleGattServer::disconnectHotspotThread(void* arg)
{
    auto* self = *(BleGattServer**)arg;
    delete (BleGattServer**)arg;

    if (!self->m_wifi) return nullptr;

    self->m_wifi->disconnectHotspot();
    self->m_wifi->wifiListCleanup();

    uint8_t st = (uint8_t)WIFI_State_DISCONNECTED;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    self->notifyChar(conn, CHAR_WIFI_STATUS, &st, 1);
    g_object_unref(conn);
    return nullptr;
}

void* BleGattServer::sendLiveStatusThread(void* arg)
{
    auto* self = *(BleGattServer**)arg;
    delete (BleGattServer**)arg;

    if (!self->m_mqtt) return nullptr;

    self->m_mqtt->stopLive();

    // Wait for live status to become non-live (timeout 5 seconds)
    for (int i = 0; i < 50; i++) {
        if (self->m_mqtt->getLiveStatus() != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    uint8_t liveStatus = (uint8_t)self->m_mqtt->getLiveStatus();
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    self->notifyChar(conn, CHAR_LIVE_STATUS, &liveStatus, 1);
    g_object_unref(conn);
    return nullptr;
}

} // namespace ft
