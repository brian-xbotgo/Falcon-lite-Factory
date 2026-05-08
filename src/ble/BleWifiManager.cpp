#include "ble/BleWifiManager.h"
#include "ble/BleUtility.h"
#include "Rk_wifi.h"
#include "cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

BleWifiManager::~BleWifiManager()
{
    wifiListCleanup();
}

std::string BleWifiManager::getStaIp(const char* ifname)
{
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return {};
    }

    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, ifname) == 0) {
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &((sockaddr_in*)ifa->ifa_addr)->sin_addr,
                          buf, sizeof(buf))) {
                freeifaddrs(ifaddr);
                return buf;
            }
        }
    }
    freeifaddrs(ifaddr);
    return {};
}

int BleWifiManager::getStaIpv6(const char* ifname, std::vector<std::string>& out, int maxCount)
{
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (auto* ifa = ifaddr; ifa && (int)out.size() < maxCount; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6 &&
            strcmp(ifa->ifa_name, ifname) == 0) {
            char buf[INET6_ADDRSTRLEN];
            auto* sa6 = (sockaddr_in6*)ifa->ifa_addr;
            if (inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof(buf)))
                out.push_back(buf);
        }
    }
    freeifaddrs(ifaddr);
    return out.empty() ? -1 : 0;
}

int BleWifiManager::getStaCount(const char* interface)
{
    // Check if hostapd is running
    FILE* fp = popen("ps | grep hostapd | grep -v grep", "r");
    if (fp) {
        char buf[256];
        if (!fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            return 0;
        }
        pclose(fp);
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hostapd_cli -i %s list_sta | wc -l", interface);
    fp = popen(cmd, "r");
    if (!fp) return -1;

    int count = -1;
    if (fscanf(fp, "%d", &count) != 1) count = 0;
    pclose(fp);
    return count;
}

int BleWifiManager::readWifiConfig(const char* interface, WifiCfg& cfg)
{
    (void)interface;
    FILE* fp = fopen(HOSTAPD_CONF, "r");
    if (!fp) return -1;

    char line[256];
    bool foundSsid = false, foundPwd = false;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* key = strtok(line, "=");
        char* value = strtok(nullptr, "\n");
        if (!key || !value) continue;
        while (*value == ' ' || *value == '\t') value++;

        if (strcmp(key, "ssid") == 0) {
            strncpy(cfg.ssid, value, sizeof(cfg.ssid) - 1);
            foundSsid = true;
        } else if (strcmp(key, "wpa_passphrase") == 0) {
            strncpy(cfg.pwd, value, sizeof(cfg.pwd) - 1);
            foundPwd = true;
        }
        if (foundSsid && foundPwd) break;
    }
    fclose(fp);
    if (!foundSsid || !foundPwd) return -1;
    strncpy(cfg.ip_addr, HOSTAP_IP, sizeof(cfg.ip_addr) - 1);
    return 0;
}

std::vector<uint8_t> BleWifiManager::getWifiCfg(const char* interface, int& outLen)
{
    WifiCfg cfg = {};
    if (readWifiConfig(interface, cfg) != 0) return {};

    int len = WifiCfgLength(&cfg);
    if (len <= 0) return {};

    std::vector<uint8_t> buf(len);
    if (WifiCfgPack(&cfg, (char*)buf.data()) != 0) return {};
    outLen = len;
    return buf;
}

int BleWifiManager::registerStateCallback(int (*handler)(uint8_t state, void* info))
{
    return RK_wifi_register_callback((RK_wifi_state_callback)handler);
}

int BleWifiManager::wifiConnect(const char* interface, const WifiCfg& cfg)
{
    (void)interface;
    LOG("Connecting to SSID: %s\n", cfg.ssid);
    if (cfg.pwd[0] == '\0') {
        return RK_wifi_connect((char*)cfg.ssid, nullptr, NONE, nullptr);
    }
    return RK_wifi_connect((char*)cfg.ssid, cfg.pwd, WPA2_WPA3, nullptr);
}

int BleWifiManager::connectHotspot(const uint8_t* data, size_t len)
{
    WifiCfg cfg = {};
    if (WifiCfgUnPack(&cfg, (const char*)data, (int)len) != 0) return RET_DATA_UNPACK_FAIL;
    return wifiConnect(STA_INTERFACE, cfg);
}

int BleWifiManager::disconnectHotspot()
{
    return RK_wifi_disconnect_network();
}

std::string BleWifiManager::parseIwScanResults(const char* scanRes)
{
    if (!scanRes) return {};

    cJSON* root = cJSON_CreateArray();
    if (!root) return {};

    const char* p = scanRes;
    while ((p = strstr(p, "BSS "))) {
        const char* next = strstr(p + 4, "BSS ");
        size_t blockLen = next ? (size_t)(next - p) : strlen(p);

        char rawSsid[256] = {};
        int rssi = 0;
        bool haveSsid = false, haveRssi = false;
        const char* s;

        if ((s = strstr(p, "SSID: ")) && s < p + blockLen) {
            s += 6;
            size_t i = 0;
            while (s[i] && s[i] != '\n' && i < sizeof(rawSsid) - 1) {
                rawSsid[i] = s[i];
                i++;
            }
            rawSsid[i] = '\0';
            haveSsid = true;
        }

        if ((s = strstr(p, "signal: ")) && s < p + blockLen) {
            rssi = (int)atof(s + 8);
            haveRssi = true;
        }

        if (haveSsid && haveRssi) {
            char ssidBin[512] = {};
            if (unescape_iw_ssid(rawSsid, ssidBin, sizeof(ssidBin)) != 0)
                goto nextBss;

            if (is_valid_utf8((const unsigned char*)ssidBin, strlen(ssidBin))) {
                cJSON* item = cJSON_CreateObject();
                if (item) {
                    cJSON_AddStringToObject(item, "ssid", ssidBin);
                    cJSON_AddNumberToObject(item, "rssi", rssi);
                    cJSON_AddItemToArray(root, item);
                }
            }
        }

    nextBss:
        if (!next) break;
        if (next <= p) break;
        p = next;
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    std::string result(json ? json : "");
    free(json);
    return result;
}

std::string BleWifiManager::scanWifi()
{
    std::lock_guard<std::mutex> lock(m_scanMutex);

    FILE* fp = popen("iw dev wlan0 scan", "r");
    if (!fp) return {};

    char buf[4096];
    std::string output;
    while (fgets(buf, sizeof(buf), fp))
        output += buf;
    pclose(fp);

    return parseIwScanResults(output.c_str());
}

bool BleWifiManager::needUpdate(const std::map<std::string, int>& dict,
                                const char* ssid, int newRssi)
{
    auto it = dict.find(ssid);
    int oldRssi = (it != dict.end()) ? it->second : -100;
    return (oldRssi == -100) || (abs(newRssi - oldRssi) >= 1);
}

void BleWifiManager::sortByRssi(WifiList& list)
{
    if (!list.wifiInfo || list.wifiNum <= 1) return;
    for (int i = 0; i < (int)list.wifiNum - 1; i++) {
        for (int j = 0; j < (int)list.wifiNum - 1 - i; j++) {
            if (list.wifiInfo[j].rssi < list.wifiInfo[j + 1].rssi) {
                WifiInfo temp = list.wifiInfo[j];
                list.wifiInfo[j] = list.wifiInfo[j + 1];
                list.wifiInfo[j + 1] = temp;
            }
        }
    }
}

int BleWifiManager::parseScanResult(const char* json, WifiList& wifiList)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return -1;

    std::lock_guard<std::mutex> lock(m_dictMutex);

    int arraySize = cJSON_GetArraySize(root);
    if (arraySize <= 0) {
        wifiList.wifiInfo = nullptr;
        wifiList.wifiNum = 0;
        cJSON_Delete(root);
        return 0;
    }

    wifiList.wifiInfo = (WifiInfo*)malloc(sizeof(WifiInfo) * arraySize);
    if (!wifiList.wifiInfo) {
        cJSON_Delete(root);
        return RET_MALLOC_FAIL;
    }

    std::map<std::string, int> localDict;
    int validCnt = 0;

    for (int i = 0; i < arraySize; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* ssid = cJSON_GetObjectItem(item, "ssid");
        cJSON* rssi = cJSON_GetObjectItem(item, "rssi");

        if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0' && cJSON_IsNumber(rssi)) {
            const char* ssidStr = ssid->valuestring;
            if (localDict.find(ssidStr) == localDict.end() &&
                needUpdate(m_ssidDict, ssidStr, rssi->valueint)) {
                m_ssidDict[ssidStr] = rssi->valueint;
                localDict[ssidStr] = rssi->valueint;
                strcpy(wifiList.wifiInfo[validCnt].ssid, ssidStr);
                wifiList.wifiInfo[validCnt].rssi = rssi->valueint;
                validCnt++;
            }
        }
    }

    wifiList.wifiNum = validCnt;
    cJSON_Delete(root);
    sortByRssi(wifiList);
    return 0;
}

void BleWifiManager::iteratorInit(WifiIterator& it, const WifiList& list)
{
    it.list = &list;
    it.currentIndex = 0;
}

int BleWifiManager::iteratorNext(WifiIterator& it, char* outBuf, int bufLen)
{
    if (!it.list || it.currentIndex >= (int)it.list->wifiNum) return -1;

    cJSON* root = cJSON_CreateObject();
    cJSON* array = cJSON_AddArrayToObject(root, "wifi_list");

    int startIndex = it.currentIndex;
    for (; it.currentIndex < (int)it.list->wifiNum; ++it.currentIndex) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", it.list->wifiInfo[it.currentIndex].ssid);
        cJSON_AddNumberToObject(item, "rssi", it.list->wifiInfo[it.currentIndex].rssi);
        cJSON_AddItemToArray(array, item);

        char* tmpStr = cJSON_PrintUnformatted(root);
        int len = strlen(tmpStr);
        if (len >= bufLen) {
            cJSON_DeleteItemFromArray(array, cJSON_GetArraySize(array) - 1);
            if (startIndex == it.currentIndex) ++it.currentIndex;
            free(tmpStr);
            break;
        }
        free(tmpStr);
    }

    char* finalStr = cJSON_PrintUnformatted(root);
    strcpy(outBuf, finalStr);
    cJSON_Delete(root);
    free(finalStr);
    return 0;
}

void BleWifiManager::wifiListCleanup()
{
    std::lock_guard<std::mutex> lock(m_dictMutex);
    m_ssidDict.clear();
}

} // namespace ft
