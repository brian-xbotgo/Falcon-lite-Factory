// WiFi utility functions — extracted from gimbal_mqtt_message.c

#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#ifndef WIFI_SSID_LEN
#define WIFI_SSID_LEN 32
#endif
#ifndef WIFI_PWD_LEN
#define WIFI_PWD_LEN  64
#endif
#ifndef WIFI_IP_LEN
#define WIFI_IP_LEN   16
#endif
#ifndef WIFI_IPV6_LEN
#define WIFI_IPV6_LEN 46
#endif

struct WifiCfg { char ssid[WIFI_SSID_LEN]; char pwd[WIFI_PWD_LEN]; char ip_addr[WIFI_IP_LEN]; };

static int json_to_string(cJSON *root, char *out, int out_size) {
    char *str = cJSON_PrintUnformatted(root);
    if (!str) return -1;
    int len = strlen(str) + 1;
    if (out && out_size >= len) {
        strncpy(out, str, len);
        out[len - 1] = '\0';
    }
    cJSON_free(str);
    return len;
}

int WifiCfgLength(const struct WifiCfg *cfg) {
    if (!cfg) return 0;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", cfg->ssid);
    cJSON_AddStringToObject(root, "password", cfg->pwd);
    cJSON_AddStringToObject(root, "ip", cfg->ip_addr);
    int len = json_to_string(root, NULL, 0);
    cJSON_Delete(root);
    return len;
}

int WifiCfgPack(const struct WifiCfg *cfg, char *infoArray) {
    if (!cfg || !infoArray) return -1;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", cfg->ssid);
    cJSON_AddStringToObject(root, "password", cfg->pwd);
    cJSON_AddStringToObject(root, "ip", cfg->ip_addr);
    int len = json_to_string(root, infoArray, 1024);
    cJSON_Delete(root);
    return (len > 0) ? 0 : -1;
}

int WifiCfgUnPack(struct WifiCfg *cfg, const char *infoArray, int arrayLen) {
    if (!cfg || !infoArray || arrayLen <= 0) return -1;
    cJSON *root = cJSON_Parse(infoArray);
    if (!root) return -1;

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    cJSON *ip = cJSON_GetObjectItem(root, "ip");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(pwd)) {
        cJSON_Delete(root);
        return -1;
    }

    snprintf(cfg->ssid, WIFI_SSID_LEN, "%s", ssid->valuestring);
    snprintf(cfg->pwd, WIFI_PWD_LEN, "%s", pwd->valuestring);
    snprintf(cfg->ip_addr, WIFI_IP_LEN, "%s", cJSON_IsString(ip) ? ip->valuestring : "");
    cJSON_Delete(root);
    return 0;
}

int WifiIpLength(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count) {
    cJSON *root = cJSON_CreateObject();
    if (ipAddr) cJSON_AddStringToObject(root, "ip", ipAddr);
    if (ipv6Addr && addr_count > 0) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < addr_count; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateString(ipv6Addr[i]));
        cJSON_AddItemToObject(root, "ipv6_list", arr);
    }
    int len = json_to_string(root, NULL, 0);
    cJSON_Delete(root);
    return len;
}

int WifiIpPack(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count, char *infoArray) {
    if (!infoArray) return -1;
    cJSON *root = cJSON_CreateObject();
    if (ipAddr) cJSON_AddStringToObject(root, "ip", ipAddr);
    if (ipv6Addr && addr_count > 0) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < addr_count; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateString(ipv6Addr[i]));
        cJSON_AddItemToObject(root, "ipv6_list", arr);
    }
    int len = json_to_string(root, infoArray, 1024);
    cJSON_Delete(root);
    return (len > 0) ? 0 : -1;
}
