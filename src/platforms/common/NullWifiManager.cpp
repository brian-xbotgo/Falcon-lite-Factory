#include "platforms/common/interface/IWifiManager.h"

namespace ft {

class NullWifiManager : public IWifiManager {
public:
    std::string getStaIp(const char*) override { return ""; }
    int getStaCount(const char*) override { return 0; }
    std::vector<uint8_t> getWifiCfg(const char*, int&) override { return {}; }
    std::string scanWifi() override { return ""; }
    int connectHotspot(const uint8_t*, size_t) override { return -1; }
    int disconnectHotspot() override { return -1; }
    int registerStateCallback(StateCallback) override { return -1; }
};

} // namespace ft
