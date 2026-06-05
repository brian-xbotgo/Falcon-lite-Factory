#include "drivers/Rk3576WifiManager.h"
#include <Rk_wifi.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <cctype>

namespace ft {

// ---- UTF-8 / iw scan helpers (inlined from old BleUtility) ----

static bool isValidUtf8(const unsigned char* s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (s[i] <= 0x7F) { i++; }
        else if ((s[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((s[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((s[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

static int hexVal(char c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int unescapeIwSsid(const char* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0) return -1;
    size_t len = std::strlen(src);
    size_t i = 0, j = 0;
    while (i < len && j < dstSize - 1) {
        if (i + 3 < len && src[i] == '\\' && src[i + 1] == 'x' &&
            std::isxdigit(static_cast<unsigned char>(src[i + 2])) &&
            std::isxdigit(static_cast<unsigned char>(src[i + 3]))) {
            int h = hexVal(src[i + 2]);
            int l = hexVal(src[i + 3]);
            dst[j++] = static_cast<char>((h << 4) | l);
            i += 4;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return (i < len) ? -1 : 0;
}

// ---- WiFiCfg pack/unpack (nlohmann/json) ----

static int wifiCfgPack(const WifiCfg* cfg, char* out, size_t outSize)
{
    if (!cfg || !out || outSize == 0) return -1;
    nlohmann::json j = { {"ssid", cfg->ssid}, {"password", cfg->pwd}, {"ip", cfg->ip_addr} };
    std::string s = j.dump();
    if (s.size() >= outSize) return -1;
    std::strcpy(out, s.c_str());
    return 0;
}

static int wifiCfgUnpack(WifiCfg* cfg, const char* data, size_t len)
{
    if (!cfg || !data || len == 0) return -1;
    try {
        nlohmann::json j = nlohmann::json::parse(data, data + len);
        std::strncpy(cfg->ssid, j.value("ssid", "").c_str(), sizeof(cfg->ssid) - 1);
        std::strncpy(cfg->pwd, j.value("password", "").c_str(), sizeof(cfg->pwd) - 1);
        std::strncpy(cfg->ip_addr, j.value("ip", "").c_str(), sizeof(cfg->ip_addr) - 1);
        cfg->ssid[sizeof(cfg->ssid) - 1] = '\0';
        cfg->pwd[sizeof(cfg->pwd) - 1] = '\0';
        cfg->ip_addr[sizeof(cfg->ip_addr) - 1] = '\0';
        return 0;
    } catch (...) {
        return -1;
    }
}

static int wifiCfgLength(const WifiCfg* cfg)
{
    if (!cfg) return 0;
    nlohmann::json j = { {"ssid", cfg->ssid}, {"password", cfg->pwd}, {"ip", cfg->ip_addr} };
    return static_cast<int>(j.dump().size()) + 1;
}

// ---- IWifiManager implementation ----

std::string Rk3576WifiManager::getStaIp(const char* ifname)
{
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return {};
    }
    std::string result;
    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
            std::strcmp(ifa->ifa_name, ifname) == 0) {
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                          buf, sizeof(buf))) {
                result = buf;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return result;
}

int Rk3576WifiManager::getStaCount(const char* interface)
{
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
    std::snprintf(cmd, sizeof(cmd), "hostapd_cli -i %s list_sta | wc -l", interface);
    fp = popen(cmd, "r");
    if (!fp) return -1;
    int count = -1;
    if (fscanf(fp, "%d", &count) != 1) count = 0;
    pclose(fp);
    return count;
}

std::vector<uint8_t> Rk3576WifiManager::getWifiCfg(const char* interface, int& outLen)
{
    (void)interface;
    outLen = 0;
    FILE* fp = std::fopen(HOSTAPD_CONF, "r");
    if (!fp) return {};

    char line[256];
    bool foundSsid = false, foundPwd = false;
    WifiCfg cfg = {};

    while (std::fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* key = std::strtok(line, "=");
        char* value = std::strtok(nullptr, "\n");
        if (!key || !value) continue;
        while (*value == ' ' || *value == '\t') value++;

        if (std::strcmp(key, "ssid") == 0) {
            std::strncpy(cfg.ssid, value, sizeof(cfg.ssid) - 1);
            foundSsid = true;
        } else if (std::strcmp(key, "wpa_passphrase") == 0) {
            std::strncpy(cfg.pwd, value, sizeof(cfg.pwd) - 1);
            foundPwd = true;
        }
        if (foundSsid && foundPwd) break;
    }
    std::fclose(fp);
    if (!foundSsid || !foundPwd) return {};

    std::strncpy(cfg.ip_addr, HOSTAP_IP, sizeof(cfg.ip_addr) - 1);
    int len = wifiCfgLength(&cfg);
    if (len <= 0) return {};

    std::vector<uint8_t> buf(len);
    if (wifiCfgPack(&cfg, reinterpret_cast<char*>(buf.data()), buf.size()) != 0) return {};
    outLen = len;
    return buf;
}

static std::string parseIwScanResults(const char* scanRes)
{
    if (!scanRes) return "[]";
    nlohmann::json array = nlohmann::json::array();

    const char* p = scanRes;
    while ((p = std::strstr(p, "BSS "))) {
        const char* next = std::strstr(p + 4, "BSS ");
        size_t blockLen = next ? static_cast<size_t>(next - p) : std::strlen(p);

        char rawSsid[256] = {};
        int rssi = 0;
        bool haveSsid = false, haveRssi = false;

        const char* s = nullptr;
        if ((s = std::strstr(p, "SSID: ")) && static_cast<size_t>(s - p) < blockLen) {
            s += 6;
            size_t i = 0;
            while (s[i] && s[i] != '\n' && i < sizeof(rawSsid) - 1) {
                rawSsid[i] = s[i];
                i++;
            }
            rawSsid[i] = '\0';
            haveSsid = true;
        }

        if ((s = std::strstr(p, "signal: ")) && static_cast<size_t>(s - p) < blockLen) {
            rssi = static_cast<int>(std::atof(s + 8));
            haveRssi = true;
        }

        if (haveSsid && haveRssi) {
            char ssidBin[512] = {};
            if (unescapeIwSsid(rawSsid, ssidBin, sizeof(ssidBin)) == 0 &&
                isValidUtf8(reinterpret_cast<unsigned char*>(ssidBin), std::strlen(ssidBin))) {
                array.push_back({ {"ssid", ssidBin}, {"rssi", rssi} });
            }
        }

        if (!next) break;
        if (next <= p) break;
        p = next;
    }
    return array.dump();
}

std::string Rk3576WifiManager::scanWifi()
{
    std::lock_guard<std::mutex> lock(scanMutex_);
    FILE* fp = popen("iw dev wlan0 scan", "r");
    if (!fp) return "[]";

    char buf[4096];
    std::string output;
    while (std::fgets(buf, sizeof(buf), fp))
        output += buf;
    pclose(fp);

    return parseIwScanResults(output.c_str());
}

int Rk3576WifiManager::connectHotspot(const uint8_t* data, size_t len)
{
    WifiCfg cfg = {};
    if (wifiCfgUnpack(&cfg, reinterpret_cast<const char*>(data), static_cast<int>(len)) != 0)
        return -1;

    if (cfg.pwd[0] == '\0') {
        return RK_wifi_connect(cfg.ssid, nullptr, NONE, nullptr);
    }
    return RK_wifi_connect(cfg.ssid, cfg.pwd, WPA2_WPA3, nullptr);
}

int Rk3576WifiManager::disconnectHotspot()
{
    return RK_wifi_disconnect_network();
}

int Rk3576WifiManager::registerStateCallback(StateCallback handler)
{
    return RK_wifi_register_callback(reinterpret_cast<RK_wifi_state_callback>(handler));
}

} // namespace ft
