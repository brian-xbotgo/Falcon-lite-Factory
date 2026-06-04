#pragma once
#include "platforms/common/interface/IWifiManager.h"
#include <cstring>

namespace ft {

class Rk3576WifiManager : public IWifiManager {
public:
    std::string getStaIp(const char*) override { return "192.168.1.100"; }
    int getStaCount(const char*) override { return 1; }
    std::vector<uint8_t> getWifiCfg(const char*, int& outLen) override {
        outLen = 0;
        return {};
    }
    std::string scanWifi() override { return "[]"; }
    int connectHotspot(const uint8_t*, size_t) override { return 0; }
    int disconnectHotspot() override { return 0; }
    int registerStateCallback(StateCallback) override { return 0; }
};

} // namespace ft
