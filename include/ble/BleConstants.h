#pragma once

#include <cstdint>

namespace ft {

// ======================== BlueZ D-Bus ========================
constexpr const char* BLUEZ_SERVICE   = "org.bluez";
constexpr const char* ADAPTER_PATH    = "/org/bluez/hci0";
constexpr const char* ADV_OBJ_PATH    = "/org/bluez/example/advertisement0";
constexpr const char* APP_ROOT        = "/org/bluez/example/server";
constexpr const char* GATT_MGR_IFACE  = "org.bluez.GattManager1";
constexpr const char* ADV_MGR_IFACE   = "org.bluez.LEAdvertisingManager1";

// ======================== MQTT ========================
constexpr const char* MQTT_HOST       = "127.0.0.1";
constexpr int         MQTT_PORT       = 1883;
constexpr const char* MQTT_CLIENT_ID  = "ble_wifi_config";
constexpr int         MQTT_KEEPALIVE  = 60;
constexpr int         MQTT_QOS        = 2;

// ======================== UUIDs ========================
constexpr const char* SERVICE_UUID    = "0000ff00-0000-1000-8000-00805f9b34fb";

constexpr const char* kCharUuids[8] = {
    "0000ff01-0000-1000-8000-00805f9b34fb",  // FF01 op_info_in
    "0000ff02-0000-1000-8000-00805f9b34fb",  // FF02 ap_info_out
    "0000ff03-0000-1000-8000-00805f9b34fb",  // FF03 hotspot_info_in
    "0000ff04-0000-1000-8000-00805f9b34fb",  // FF04 wifi_list_out
    "0000ff05-0000-1000-8000-00805f9b34fb",  // FF05 wifi_status_out
    "0000ff06-0000-1000-8000-00805f9b34fb",  // FF06 wifi_ip_out
    "0000ff07-0000-1000-8000-00805f9b34fb",  // FF07 live_status_out
    "0000ff08-0000-1000-8000-00805f9b34fb",  // FF08 app_token_in
};

constexpr int CHAR_OP_INFO       = 0;
constexpr int CHAR_AP_INFO       = 1;
constexpr int CHAR_HOTSPOT_INFO  = 2;
constexpr int CHAR_WIFI_LIST     = 3;
constexpr int CHAR_WIFI_STATUS   = 4;
constexpr int CHAR_WIFI_IP       = 5;
constexpr int CHAR_LIVE_STATUS   = 6;
constexpr int CHAR_APP_TOKEN     = 7;
constexpr int CHAR_COUNT         = 8;

// ======================== Advertisement ========================
constexpr int MANUFACTURER_ID    = 0xFFFF;
constexpr int ADV_MIN_INTERVAL   = 20;
constexpr int ADV_MAX_INTERVAL   = 300;

// ======================== WiFi States ========================
enum WifiState {
    WIFI_State_IDLE = 0,
    WIFI_State_CONNECTING,
    WIFI_State_CONNECTFAILED,
    WIFI_State_CONNECTFAILED_WRONG_KEY,
    WIFI_State_CONNECTED,
    WIFI_State_DISCONNECTED,
    WIFI_State_OPEN,
    WIFI_State_OFF,
    WIFI_State_SCAN_RESULTS,
    WIFI_State_DHCP_OK,
};

// ======================== OP Codes ========================
enum OpCode {
    OP_GET_AP_INFO        = 0,
    OP_GET_WIFI_LIST      = 1,
    OP_GET_WIFI_STATUS    = 2,
    OP_DISCONNECT_HOTSPOT = 3,
    OP_STOP_LIVE          = 4,
};

// ======================== Sizes & Paths ========================
constexpr int SN_LEN           = 14;
constexpr int BLE_MTU_LEN      = 180;
constexpr int BLE_SAFE_LEN     = 200;
constexpr int MAX_ALIAS_LEN    = 13;
constexpr int MAX_CHAR_VAL     = 256;
constexpr int BLE_NAME_MAX     = 32;

constexpr const char* AP_INTERFACE  = "wlan1";
constexpr const char* STA_INTERFACE = "wlan0";
constexpr const char* HOSTAPD_CONF  = "/tmp/wps_hostapd.conf";
constexpr const char* HOSTAP_IP     = "192.168.5.1";
constexpr const char* SSID_FILE     = "/tmp/wps_hostapd.conf";
constexpr const char* DEVICE_ALIAS_FILE = "/userdata/device_alias.txt";
constexpr const char* INFO_INI_PATH = "/device_data/info.ini";
constexpr const char* FACTORY_FLAG  = "/userdata/factory_mode";
constexpr const char* SN_FILE       = "/device_data/pcba.txt";

// ======================== Device Colors ========================
enum DeviceColor {
    DEVICE_COLOR_UNKNOWN   = 0,
    DEVICE_COLOR_GREEN     = 1,
    DEVICE_COLOR_PINK      = 2,
    DEVICE_COLOR_DARK_GREY = 3,
};

// ======================== Error Codes ========================
constexpr int RET_OK                    = 0;
constexpr int RET_INVALID_PARAM         = -1001;
constexpr int RET_NULL_POINTER          = -1002;
constexpr int RET_BUFFER_TOO_SMALL      = -1003;
constexpr int RET_CMD_FAILED            = -1010;
constexpr int RET_FILE_OPEN_FAIL        = -1011;
constexpr int RET_FILE_READ_FAIL        = -1012;
constexpr int RET_MALLOC_FAIL           = -1020;
constexpr int RET_NO_DATA               = -1030;
constexpr int RET_DATA_PARSE_FAIL       = -1031;
constexpr int RET_DATA_PACK_FAIL        = -1032;
constexpr int RET_DATA_UNPACK_FAIL      = -1033;
constexpr int RET_INVALID_JSON          = -1034;
constexpr int RET_WIFI_SCAN_FAIL        = -1040;
constexpr int RET_WIFI_CONNECT_FAIL     = -1041;
constexpr int RET_WIFI_CONNECT_FAILED   = -1042;
constexpr int RET_WIFI_DISCONNECT_FAIL  = -1043;
constexpr int RET_WIFI_GET_IP_FAIL      = -1044;
constexpr int RET_BLE_INIT_FAIL         = -1050;
constexpr int RET_BLE_ADVERTISE_FAIL    = -1051;
constexpr int RET_BLE_GATT_REG_FAIL     = -1052;
constexpr int RET_OPERATION_NOT_PERMIT  = -1060;
constexpr int RET_OPERATION_TIMEOUT     = -1061;
constexpr int RET_NOT_IMPLEMENTED       = -1062;
constexpr int RET_ITERATOR_END          = -1063;

} // namespace ft
