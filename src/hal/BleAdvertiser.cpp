// BLE factory test HAL implementation — self-contained (only deps: mosquitto + glib/gio)
// Converts the standalone C module into a C++ HAL class per project architecture.

#include "hal/BleAdvertiser.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <gio/gio.h>
#include <mosquitto.h>

namespace ft {

static constexpr int SN_LEN          = 14;
static constexpr int MANUFACTURER_ID = 0xFFFF;
#define MQTT_HOST       "127.0.0.1"
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "ble_factory"
#define MQTT_KEEPALIVE  60
#define BLUEZ_SERVICE   "org.bluez"
#define ADAPTER_PATH    "/org/bluez/hci0"
#define ADV_OBJ_PATH    "/org/bluez/example/advertisement0"
#define APP_ROOT        "/org/bluez/example/server"
#define SERVICE_UUID    "0000ff00-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID       "0000ff01-0000-1000-8000-00805f9b34fb"

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_factory] " fmt, ##__VA_ARGS__)

BleAdvertiser::~BleAdvertiser() { shutdown(); }

// ======================== MQTT callbacks ========================
static void mqtt_msg_cb(mosquitto*, void* ud, const mosquitto_message* msg) {
    auto* self = static_cast<BleAdvertiser*>(ud);
    if (!msg || !msg->payload || self->m_sn_valid) return;
    if ((!strcmp(msg->topic, "10R") || !strcmp(msg->topic, "31R"))
        && msg->payloadlen >= SN_LEN) {
        memcpy(self->m_sn, msg->payload, SN_LEN);
        self->m_sn_valid = true;
        LOG("SN: %.*s\n", SN_LEN, self->m_sn);
        self->registerAdvertisement();
        self->registerGattServer();
    }
}

static void mqtt_conn_cb(mosquitto* m, void*, int rc) {
    if (rc) { LOG("MQTT err %d\n", rc); return; }
    mosquitto_subscribe(m, nullptr, "10R", 2);
    mosquitto_subscribe(m, nullptr, "31R", 2);
    LOG("MQTT connected, waiting for SN...\n");
}

int BleAdvertiser::setupMqtt() {
    mosquitto_lib_init();
    auto* mosq = mosquitto_new(MQTT_CLIENT_ID, true, this);
    if (!mosq) return -1;
    m_mosq = mosq;
    mosquitto_message_callback_set((mosquitto*)m_mosq, mqtt_msg_cb);
    mosquitto_connect_callback_set((mosquitto*)m_mosq, mqtt_conn_cb);
    return mosquitto_connect((mosquitto*)m_mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE);
}

// ======================== BLE Advertisement ========================
static const char* g_adv_xml =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='ManufacturerData' type='a{qv}' access='read'/>"
    "    <property name='Discoverable' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static GVariant* adv_get_prop(GDBusConnection*, const gchar*, const gchar*,
                               const gchar*, const gchar* name, GError**, gpointer ud) {
    auto* self = static_cast<BleAdvertiser*>(ud);
    if (!g_strcmp0(name, "Type"))           return g_variant_new_string("peripheral");
    if (!g_strcmp0(name, "ServiceUUIDs"))   return g_variant_new_strv(nullptr, 0);
    if (!g_strcmp0(name, "Discoverable"))   return g_variant_new_boolean(TRUE);
    if (!g_strcmp0(name, "ManufacturerData") && self->m_sn_valid) {
        GVariantBuilder ab;
        g_variant_builder_init(&ab, G_VARIANT_TYPE("ay"));
        for (int i = 0; i < SN_LEN; i++) g_variant_builder_add(&ab, "y", self->m_sn[i]);
        GVariant* data = g_variant_builder_end(&ab);
        GVariantBuilder mb;
        g_variant_builder_init(&mb, G_VARIANT_TYPE("a{qv}"));
        g_variant_builder_add(&mb, "{qv}", (guint16)MANUFACTURER_ID, data);
        return g_variant_builder_end(&mb);
    }
    if (!g_strcmp0(name, "ManufacturerData"))
        return g_variant_new_array(G_VARIANT_TYPE("{qv}"), nullptr, 0);
    return nullptr;
}

static void adv_release(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                         const gchar*, GVariant*, GDBusMethodInvocation* inv, gpointer) {
    g_dbus_method_invocation_return_value(inv, nullptr);
}

static GDBusInterfaceVTable g_adv_vtable = { adv_release, adv_get_prop, nullptr, nullptr };

int BleAdvertiser::registerAdvertisement() {
    auto* conn = (GDBusConnection*)m_conn;
    GError* err = nullptr;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(g_adv_xml, &err);
    if (!node) { LOG("adv XML: %s\n", err->message); g_error_free(err); return -1; }
    g_dbus_connection_register_object(conn, ADV_OBJ_PATH, node->interfaces[0],
                                       &g_adv_vtable, this, nullptr, &err);
    if (err) { LOG("adv obj: %s\n", err->message); g_error_free(err); return -1; }

    GVariant* reply = g_dbus_connection_call_sync(conn, BLUEZ_SERVICE, ADAPTER_PATH,
        "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
        g_variant_new("(o@a{sv})", ADV_OBJ_PATH, g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
    if (!reply) { LOG("RegisterAdvertisement: %s\n", err->message); g_error_free(err); return -1; }
    g_variant_unref(reply);
    m_adv_registered = true;
    LOG("BLE advertising SN: %.*s\n", SN_LEN, m_sn);
    return 0;
}

// ======================== GATT Server ========================
static const gchar* g_svc_xml =
    "<node>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar* g_chr_xml =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <method name='WriteValue'><arg name='value' type='ay' direction='in'/></method>"
    "    <method name='ReadValue'><arg name='value' type='ay' direction='out'/></method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "  </interface>"
    "</node>";

static const gchar* g_om_xml =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GVariant* gatt_get_prop(GDBusConnection*, const gchar*, const gchar* path,
                                const gchar*, const gchar* name, GError**, gpointer ud) {
    auto* self = static_cast<BleAdvertiser*>(ud);
    bool isSvc = !g_strcmp0(path, APP_ROOT "/service00");
    bool isChr = !g_strcmp0(path, APP_ROOT "/service00/char0000");

    if (!g_strcmp0(name, "UUID")) {
        if (isSvc) return g_variant_new_string(SERVICE_UUID);
        if (isChr) return g_variant_new_string(CHAR_UUID);
    }
    if (!g_strcmp0(name, "Primary") && isSvc) return g_variant_new_boolean(TRUE);
    if (!g_strcmp0(name, "Flags") && isChr) {
        const gchar* f[] = {"write", "write-without-response", "read", nullptr};
        return g_variant_new_strv(f, -1);
    }
    if (!g_strcmp0(name, "Service") && isChr) return g_variant_new_string(APP_ROOT "/service00");
    if (!g_strcmp0(name, "Notifying") && isChr) return g_variant_new_boolean(FALSE);
    if (!g_strcmp0(name, "Value") && isChr && self->m_sn_valid) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ay"));
        for (int i = 0; i < SN_LEN; i++) g_variant_builder_add(&b, "y", self->m_sn[i]);
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

static void gatt_dispatch(GDBusConnection*, const gchar*, const gchar*,
                           const gchar*, const gchar* method, GVariant* params,
                           GDBusMethodInvocation* inv, gpointer ud) {
    auto* self = static_cast<BleAdvertiser*>(ud);
    if (!g_strcmp0(method, "WriteValue")) {
        GVariant* val = g_variant_get_child_value(params, 0);
        gsize n; g_variant_get_fixed_array(val, &n, 1);
        LOG("GATT write: %zu bytes\n", n);
        g_variant_unref(val);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (!g_strcmp0(method, "ReadValue")) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ay"));
        for (int i = 0; i < SN_LEN; i++) g_variant_builder_add(&b, "y", self->m_sn[i]);
        GVariant* v = g_variant_builder_end(&b);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", g_variant_new_variant(v)));
    } else {
        g_dbus_method_invocation_return_value(inv, nullptr);  // StartNotify / StopNotify
    }
}

static void om_get_objects(GDBusConnection*, const gchar*, const gchar*,
                            const gchar*, const gchar*, GVariant*,
                            GDBusMethodInvocation* inv, gpointer) {
    // Build leaf arrays first, then wrap in containing dicts.
    // Each g_variant_builder_end() must finish before its result is used.
    GVariantBuilder svc_arr;  // a{sv} for service properties
    g_variant_builder_init(&svc_arr, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&svc_arr, "{sv}", "UUID", g_variant_new_string(SERVICE_UUID));
    g_variant_builder_add(&svc_arr, "{sv}", "Primary", g_variant_new_boolean(TRUE));
    GVariant* svc_props = g_variant_builder_end(&svc_arr);

    GVariantBuilder chr_arr;  // a{sv} for characteristic properties
    g_variant_builder_init(&chr_arr, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&chr_arr, "{sv}", "UUID", g_variant_new_string(CHAR_UUID));
    g_variant_builder_add(&chr_arr, "{sv}", "Service", g_variant_new_string(APP_ROOT "/service00"));
    const gchar* fl[] = {"write", "write-without-response", "read", NULL};
    g_variant_builder_add(&chr_arr, "{sv}", "Flags", g_variant_new_strv(fl, -1));
    GVariant* chr_props = g_variant_builder_end(&chr_arr);

    // Wrap each properties array into a{sa{sv}}
    GVariantBuilder svc_if;  // a{sa{sv}}
    g_variant_builder_init(&svc_if, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_add(&svc_if, "{sv}", "org.bluez.GattService1", svc_props);
    GVariant* svc_entry = g_variant_builder_end(&svc_if);

    GVariantBuilder chr_if;  // a{sa{sv}}
    g_variant_builder_init(&chr_if, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_add(&chr_if, "{sv}", "org.bluez.GattCharacteristic1", chr_props);
    GVariant* chr_entry = g_variant_builder_end(&chr_if);

    // Build root a{oa{sa{sv}}}
    GVariantBuilder root;
    g_variant_builder_init(&root, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    g_variant_builder_add(&root, "{o@a{sa{sv}}}", APP_ROOT "/service00", svc_entry);
    g_variant_builder_add(&root, "{o@a{sa{sv}}}", APP_ROOT "/service00/char0000", chr_entry);
    GVariant* r = g_variant_builder_end(&root);
    g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})", r));
}

static GDBusInterfaceVTable g_svc_vtable  = { nullptr, gatt_get_prop, nullptr, {}};
static GDBusInterfaceVTable g_chr_vtable  = { gatt_dispatch, gatt_get_prop };
static GDBusInterfaceVTable g_om_vtable   = { om_get_objects };

int BleAdvertiser::registerGattServer() {
    auto* conn = (GDBusConnection*)m_conn;
    GError* err = nullptr;

    GDBusNodeInfo* svc = g_dbus_node_info_new_for_xml(g_svc_xml, &err);
    if (!svc) { LOG("svc XML: %s\n", err->message); g_error_free(err); return -1; }
    GDBusNodeInfo* chr = g_dbus_node_info_new_for_xml(g_chr_xml, &err);
    if (!chr) { LOG("chr XML: %s\n", err->message); g_error_free(err); return -1; }
    GDBusNodeInfo* om  = g_dbus_node_info_new_for_xml(g_om_xml, &err);
    if (!om)  { LOG("om XML: %s\n", err->message); g_error_free(err); return -1; }

    g_dbus_connection_register_object(conn, APP_ROOT "/service00", svc->interfaces[0],
                                       &g_svc_vtable, this, nullptr, &err);
    if (err) { LOG("svc obj: %s\n", err->message); g_clear_error(&err); }
    g_dbus_connection_register_object(conn, APP_ROOT "/service00/char0000", chr->interfaces[0],
                                       &g_chr_vtable, this, nullptr, &err);
    if (err) { LOG("chr obj: %s\n", err->message); g_clear_error(&err); }
    g_dbus_connection_register_object(conn, APP_ROOT, om->interfaces[0],
                                       &g_om_vtable, this, nullptr, &err);
    if (err) { LOG("om obj: %s\n", err->message); g_clear_error(&err); }

    GVariant* reply = g_dbus_connection_call_sync(conn, BLUEZ_SERVICE, ADAPTER_PATH,
        "org.bluez.GattManager1", "RegisterApplication",
        g_variant_new("(o@a{sv})", APP_ROOT, g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
    if (!reply) {
        LOG("RegisterApplication: %s (advertising-only mode)\n", err->message);
        g_clear_error(&err);
        return 0;  // non-fatal: BLE advertising still works
    }
    g_variant_unref(reply);
    m_gatt_registered = true;
    LOG("GATT server registered\n");
    return 0;
}

// ======================== D-Bus + main loop ========================
int BleAdvertiser::setupDbus() {
    GError* err = nullptr;
    m_conn = (void*)g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (!m_conn) { LOG("D-Bus: %s\n", err->message); g_error_free(err); return -1; }
    return 0;
}

int BleAdvertiser::run() {
    m_running = true;

    if (setupMqtt() != MOSQ_ERR_SUCCESS) { LOG("MQTT fail\n"); return -1; }
    mosquitto_loop_start((mosquitto*)m_mosq);

    if (setupDbus() != 0) return -1;

    m_loop = (void*)g_main_loop_new(nullptr, FALSE);
    g_main_loop_run((GMainLoop*)m_loop);
    return 0;
}

void BleAdvertiser::shutdown() {
    if (!m_running) return;
    m_running = false;

    if (m_loop) { g_main_loop_quit((GMainLoop*)m_loop); }

    auto* conn = (GDBusConnection*)m_conn;
    if (m_adv_registered && conn) {
        g_dbus_connection_call_sync(conn, BLUEZ_SERVICE, ADAPTER_PATH,
            "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
            g_variant_new("(o)", ADV_OBJ_PATH), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    }
    if (m_gatt_registered && conn) {
        g_dbus_connection_call_sync(conn, BLUEZ_SERVICE, ADAPTER_PATH,
            "org.bluez.GattManager1", "UnregisterApplication",
            g_variant_new("(o)", APP_ROOT), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    }
    if (m_mosq) {
        mosquitto_loop_stop((mosquitto*)m_mosq, false);
        mosquitto_destroy((mosquitto*)m_mosq);
        mosquitto_lib_cleanup();
    }
    if (conn) g_object_unref(conn);
    if (m_loop) g_main_loop_unref((GMainLoop*)m_loop);
}

} // namespace ft
