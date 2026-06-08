#include "ble/BleWifiManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ft {

namespace {

std::string runCommand(const char* cmd)
{
    std::string out;
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        return out;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        out += buf;
    }
    pclose(fp);
    return out;
}

std::string trim(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t pos = 0;
    while (pos < value.size() && (value[pos] == ' ' || value[pos] == '\t')) {
        ++pos;
    }
    if (pos > 0) {
        value.erase(0, pos);
    }
    return value;
}

std::string readHostapdValue(const char* key)
{
    std::ifstream in(HOSTAPD_CONF);
    if (!in.is_open()) {
        in.open("/oem/usr/conf/wps_hostapd.conf");
    }
    std::string line;
    const std::string prefix = std::string(key) + "=";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return trim(line.substr(prefix.size()));
        }
    }
    return {};
}

void copyString(char* dst, size_t dstLen, const std::string& src)
{
    if (!dst || dstLen == 0) {
        return;
    }
    std::snprintf(dst, dstLen, "%s", src.c_str());
}

} // namespace

BleWifiManager::~BleWifiManager()
{
    wifiListCleanup();
}

std::string BleWifiManager::getStaIp(const char* ifname)
{
    if (!ifname || !*ifname) {
        return {};
    }
    std::string cmd = "ip -4 addr show ";
    cmd += ifname;
    cmd += " 2>/dev/null | awk '/inet / {print $2; exit}' | cut -d/ -f1";
    auto ip = trim(runCommand(cmd.c_str()));
    if (!ip.empty()) {
        return ip;
    }

    cmd = "ifconfig ";
    cmd += ifname;
    cmd += " 2>/dev/null | awk '/inet / {print $2; exit}'";
    return trim(runCommand(cmd.c_str()));
}

int BleWifiManager::getStaIpv6(const char* ifname, std::vector<std::string>& out, int maxCount)
{
    out.clear();
    if (!ifname || !*ifname || maxCount <= 0) {
        return 0;
    }
    std::string cmd = "ip -6 addr show ";
    cmd += ifname;
    cmd += " scope global 2>/dev/null | awk '/inet6 / {print $2}' | cut -d/ -f1";
    std::istringstream lines(runCommand(cmd.c_str()));
    std::string line;
    while (static_cast<int>(out.size()) < maxCount && std::getline(lines, line)) {
        line = trim(line);
        if (!line.empty()) {
            out.push_back(line);
        }
    }
    return static_cast<int>(out.size());
}

int BleWifiManager::getStaCount(const char* interface)
{
    if (!interface || !*interface) {
        return -1;
    }
    std::string cmd = "hostapd_cli -i ";
    cmd += interface;
    cmd += " all_sta 2>/dev/null | grep -E '^[0-9a-fA-F:]{17}$' | wc -l";
    auto count = trim(runCommand(cmd.c_str()));
    if (count.empty()) {
        return 0;
    }
    return std::atoi(count.c_str());
}

std::vector<uint8_t> BleWifiManager::getWifiCfg(const char*, int& outLen)
{
    WifiCfg cfg = {};
    copyString(cfg.ssid, sizeof(cfg.ssid), readHostapdValue("ssid"));
    copyString(cfg.pwd, sizeof(cfg.pwd), readHostapdValue("wpa_passphrase"));
    copyString(cfg.ip_addr, sizeof(cfg.ip_addr), HOSTAP_IP);

    outLen = WifiCfgLength(&cfg);
    if (outLen <= 0) {
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(outLen));
    if (WifiCfgPack(&cfg, reinterpret_cast<char*>(data.data())) != 0) {
        outLen = 0;
        return {};
    }
    return data;
}

std::string BleWifiManager::scanWifi()
{
    std::lock_guard<std::mutex> lock(m_scanMutex);
    return parseIwScanResults(runCommand("iw dev wlan0 scan 2>/dev/null").c_str());
}

int BleWifiManager::parseScanResult(const char* json, WifiList& wifiList)
{
    wifiList = {};
    if (!json || !*json) {
        return -1;
    }

    std::vector<WifiInfo> items;
    std::istringstream lines(json);
    std::string line;
    while (std::getline(lines, line) && items.size() < MAX_WIFI_NUM) {
        auto comma = line.rfind(',');
        if (comma == std::string::npos) {
            continue;
        }
        auto ssid = trim(line.substr(0, comma));
        auto rssiText = trim(line.substr(comma + 1));
        if (ssid.empty()) {
            continue;
        }
        WifiInfo info = {};
        copyString(info.ssid, sizeof(info.ssid), ssid);
        info.rssi = std::atoi(rssiText.c_str());
        items.push_back(info);
    }

    if (items.empty()) {
        return -1;
    }

    wifiList.wifiNum = static_cast<uint8_t>(items.size());
    wifiList.wifiInfo = static_cast<WifiInfo*>(std::calloc(items.size(), sizeof(WifiInfo)));
    if (!wifiList.wifiInfo) {
        wifiList.wifiNum = 0;
        return -1;
    }
    std::memcpy(wifiList.wifiInfo, items.data(), items.size() * sizeof(WifiInfo));
    return 0;
}

int BleWifiManager::connectHotspot(const uint8_t* data, size_t len)
{
    if (!data || len == 0) {
        return -1;
    }

    WifiCfg cfg = {};
    if (WifiCfgUnPack(&cfg, reinterpret_cast<const char*>(data), static_cast<int>(len)) != 0) {
        return -1;
    }
    return wifiConnect(STA_INTERFACE, cfg);
}

int BleWifiManager::disconnectHotspot()
{
    return std::system("wpa_cli -i wlan0 disconnect >/dev/null 2>&1");
}

int BleWifiManager::registerStateCallback(int (*)(uint8_t, void*))
{
    return 0;
}

void BleWifiManager::iteratorInit(WifiIterator& it, const WifiList& list)
{
    it.list = &list;
    it.currentIndex = 0;
}

int BleWifiManager::iteratorNext(WifiIterator& it, char* outBuf, int bufLen)
{
    if (!outBuf || bufLen <= 0 || !it.list || it.currentIndex >= it.list->wifiNum) {
        return -1;
    }

    const auto& info = it.list->wifiInfo[it.currentIndex++];
    std::snprintf(outBuf, static_cast<size_t>(bufLen), "%s,%d", info.ssid, info.rssi);
    return 0;
}

void BleWifiManager::wifiListCleanup()
{
    std::lock_guard<std::mutex> lock(m_dictMutex);
    m_ssidDict.clear();
}

bool BleWifiManager::needUpdate(const std::map<std::string, int>& dict,
                                const char* ssid, int newRssi)
{
    auto it = dict.find(ssid ? ssid : "");
    return it == dict.end() || newRssi > it->second;
}

int BleWifiManager::readWifiConfig(const char*, WifiCfg& cfg)
{
    std::memset(&cfg, 0, sizeof(cfg));
    copyString(cfg.ssid, sizeof(cfg.ssid), readHostapdValue("ssid"));
    copyString(cfg.pwd, sizeof(cfg.pwd), readHostapdValue("wpa_passphrase"));
    copyString(cfg.ip_addr, sizeof(cfg.ip_addr), HOSTAP_IP);
    return cfg.ssid[0] ? 0 : -1;
}

int BleWifiManager::wifiConnect(const char* interface, const WifiCfg& cfg)
{
    if (!interface || !*interface || cfg.ssid[0] == '\0') {
        return -1;
    }

    std::string conf = "/tmp/factory_wpa_supplicant.conf";
    {
        std::ofstream out(conf);
        if (!out.is_open()) {
            return -1;
        }
        out << "ctrl_interface=/var/run/wpa_supplicant\n";
        out << "update_config=1\n";
        out << "network={\n";
        out << "    ssid=\"" << cfg.ssid << "\"\n";
        out << "    psk=\"" << cfg.pwd << "\"\n";
        out << "}\n";
    }

    std::string cmd = "killall -q wpa_supplicant; ifconfig ";
    cmd += interface;
    cmd += " up; wpa_supplicant -B -Dnl80211 -i";
    cmd += interface;
    cmd += " -c";
    cmd += conf;
    cmd += " >/dev/null 2>&1";
    return std::system(cmd.c_str());
}

std::string BleWifiManager::parseIwScanResults(const char* scanRes)
{
    std::istringstream lines(scanRes ? scanRes : "");
    std::string line;
    std::string ssid;
    int signal = -100;
    std::map<std::string, int> best;

    auto flush = [&]() {
        if (!ssid.empty() && needUpdate(best, ssid.c_str(), signal)) {
            best[ssid] = signal;
        }
        ssid.clear();
        signal = -100;
    };

    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.rfind("BSS ", 0) == 0) {
            flush();
        } else if (line.rfind("SSID:", 0) == 0) {
            ssid = trim(line.substr(5));
        } else if (line.rfind("signal:", 0) == 0) {
            auto value = trim(line.substr(7));
            signal = std::atoi(value.c_str());
        }
    }
    flush();

    std::vector<std::pair<std::string, int>> items(best.begin(), best.end());
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::ostringstream out;
    int count = 0;
    for (const auto& item : items) {
        if (count++ >= MAX_WIFI_NUM) {
            break;
        }
        out << item.first << "," << item.second << "\n";
    }
    return out.str();
}

void BleWifiManager::sortByRssi(WifiList& list)
{
    if (!list.wifiInfo || list.wifiNum == 0) {
        return;
    }
    std::sort(list.wifiInfo, list.wifiInfo + list.wifiNum,
              [](const WifiInfo& a, const WifiInfo& b) { return a.rssi > b.rssi; });
}

} // namespace ft

extern "C" {

int WifiCfgLength(const struct WifiCfg* cfg)
{
    if (!cfg) {
        return 0;
    }
    return 3 + static_cast<int>(std::strlen(cfg->ssid)) +
           static_cast<int>(std::strlen(cfg->pwd)) +
           static_cast<int>(std::strlen(cfg->ip_addr));
}

int WifiCfgPack(const struct WifiCfg* cfg, char* infoArray)
{
    if (!cfg || !infoArray) {
        return -1;
    }
    unsigned char ssidLen = static_cast<unsigned char>(std::min<size_t>(std::strlen(cfg->ssid), WIFI_SSID_LEN - 1));
    unsigned char pwdLen = static_cast<unsigned char>(std::min<size_t>(std::strlen(cfg->pwd), WIFI_PWD_LEN - 1));
    unsigned char ipLen = static_cast<unsigned char>(std::min<size_t>(std::strlen(cfg->ip_addr), WIFI_IP_LEN - 1));
    char* out = infoArray;
    *out++ = static_cast<char>(ssidLen);
    std::memcpy(out, cfg->ssid, ssidLen);
    out += ssidLen;
    *out++ = static_cast<char>(pwdLen);
    std::memcpy(out, cfg->pwd, pwdLen);
    out += pwdLen;
    *out++ = static_cast<char>(ipLen);
    std::memcpy(out, cfg->ip_addr, ipLen);
    return 0;
}

int WifiCfgUnPack(struct WifiCfg* cfg, const char* infoArray, int arrayLen)
{
    if (!cfg || !infoArray || arrayLen <= 0) {
        return -1;
    }
    std::memset(cfg, 0, sizeof(*cfg));
    const unsigned char* p = reinterpret_cast<const unsigned char*>(infoArray);
    int left = arrayLen;

    auto readField = [&](char* dst, int dstLen) -> bool {
        if (left <= 0) {
            return false;
        }
        int len = *p++;
        --left;
        if (len < 0 || len > left || len >= dstLen) {
            return false;
        }
        std::memcpy(dst, p, static_cast<size_t>(len));
        dst[len] = '\0';
        p += len;
        left -= len;
        return true;
    };

    return readField(cfg->ssid, WIFI_SSID_LEN) &&
           readField(cfg->pwd, WIFI_PWD_LEN) &&
           readField(cfg->ip_addr, WIFI_IP_LEN) ? 0 : -1;
}

int WifiIpLength(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count)
{
    int len = 2 + static_cast<int>(std::strlen(ipAddr ? ipAddr : ""));
    for (int i = 0; i < addr_count; ++i) {
        len += 1 + static_cast<int>(std::strlen(ipv6Addr[i]));
    }
    return len;
}

int WifiIpPack(char* ipAddr, char ipv6Addr[][WIFI_IPV6_LEN], int addr_count, char* infoArray)
{
    if (!infoArray) {
        return -1;
    }
    char* out = infoArray;
    size_t ipLen = std::strlen(ipAddr ? ipAddr : "");
    *out++ = static_cast<char>(std::min<size_t>(ipLen, WIFI_IP_LEN - 1));
    std::memcpy(out, ipAddr ? ipAddr : "", std::min<size_t>(ipLen, WIFI_IP_LEN - 1));
    out += std::min<size_t>(ipLen, WIFI_IP_LEN - 1);
    *out++ = static_cast<char>(addr_count);
    for (int i = 0; i < addr_count; ++i) {
        size_t len = std::min<size_t>(std::strlen(ipv6Addr[i]), WIFI_IPV6_LEN - 1);
        *out++ = static_cast<char>(len);
        std::memcpy(out, ipv6Addr[i], len);
        out += len;
    }
    return 0;
}

} // extern "C"
