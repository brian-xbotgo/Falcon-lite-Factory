#pragma once
// WiFi type definitions (extracted from gimbal_mqtt_message.h for standalone build)

#include <cstdint>

#define WIFI_SSID_LEN 32
#define WIFI_PWD_LEN  64
#define MAX_WIFI_NUM  10
#define WIFI_IP_LEN   16
#define WIFI_IPV6_LEN 46

struct WifiCfg
{
    char ssid[WIFI_SSID_LEN];
    char pwd[WIFI_PWD_LEN];
    char ip_addr[WIFI_IP_LEN];
};

struct WifiInfo
{
    char ssid[WIFI_SSID_LEN];
    int rssi;
};

struct WifiList
{
    uint8_t wifiNum;
    struct WifiInfo* wifiInfo;
};

#ifdef __cplusplus
extern "C" {
#endif

int WifiCfgLength(const struct WifiCfg *cfg);
int WifiCfgPack(const struct WifiCfg *cfg, char *infoArray);
int WifiCfgUnPack(struct WifiCfg *cfg, const char *infoArray, int arrayLen);
int WifiIpLength(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count);
int WifiIpPack(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count, char *infoArray);

#ifdef __cplusplus
}
#endif
