#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ft {

struct WifiCfg {
    char ssid[64];
    char pwd[64];
    char ip_addr[16];
};

struct WifiInfo {
    char ssid[64];
    int rssi;
};

class IWifiManager {
public:
    virtual ~IWifiManager() = default;
    virtual std::string getStaIp(const char* ifname) = 0;
    virtual int getStaCount(const char* interface) = 0;
    virtual std::vector<uint8_t> getWifiCfg(const char* interface, int& outLen) = 0;
    virtual std::string scanWifi() = 0;
    virtual int connectHotspot(const uint8_t* data, size_t len) = 0;
    virtual int disconnectHotspot() = 0;

    using StateCallback = int (*)(uint8_t state, void* info);
    virtual int registerStateCallback(StateCallback handler) = 0;
};

} // namespace ft
