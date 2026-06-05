#pragma once
#include "platforms/common/interface/IWifiManager.h"
#include <mutex>

namespace ft {

class Rk3576WifiManager : public IWifiManager {
public:
    std::string getStaIp(const char* ifname) override;
    int getStaCount(const char* interface) override;
    std::vector<uint8_t> getWifiCfg(const char* interface, int& outLen) override;
    std::string scanWifi() override;
    int connectHotspot(const uint8_t* data, size_t len) override;
    int disconnectHotspot() override;
    int registerStateCallback(StateCallback handler) override;

private:
    static constexpr const char* HOSTAPD_CONF = "/tmp/wps_hostapd.conf";
    static constexpr const char* HOSTAP_IP    = "192.168.5.1";
    static constexpr const char* STA_IFACE    = "wlan0";

    std::mutex scanMutex_;
};

} // namespace ft
