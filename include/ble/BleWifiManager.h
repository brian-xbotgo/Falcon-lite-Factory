#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include "ble/BleConstants.h"
#include "gimbal_mqtt_message.h"

namespace ft {

struct WifiIterator {
    const WifiList* list = nullptr;
    int currentIndex = 0;
};

class BleWifiManager {
public:
    ~BleWifiManager();

    // Get IPv4 address for an interface. Returns empty string on failure.
    std::string getStaIp(const char* ifname);

    // Get IPv6 addresses for an interface. Returns count of addresses found.
    int getStaIpv6(const char* ifname, std::vector<std::string>& out, int maxCount = 4);

    // Count connected WiFi stations on hostapd interface. Returns -1 on error.
    int getStaCount(const char* interface);

    // Get AP WiFi config packed for BLE transfer. Returns packed data, empty on failure.
    std::vector<uint8_t> getWifiCfg(const char* interface, int& outLen);

    // Scan WiFi networks via `iw dev wlan0 scan`. Returns JSON string.
    std::string scanWifi();

    // Parse JSON scan result into deduplicated, RSSI-sorted WifiList.
    // Caller must free: free(wifiList.wifiInfo)
    int parseScanResult(const char* json, WifiList& wifiList);

    // Connect to a hotspot. data is packed WifiCfg from FF03 characteristic.
    int connectHotspot(const uint8_t* data, size_t len);

    // Disconnect from currently connected WiFi.
    int disconnectHotspot();

    // Register callback for WiFi state changes (from RK WiFi stack).
    int registerStateCallback(int (*handler)(uint8_t state, void* info));

    // Iterator for paginating WiFi list across BLE MTU-sized chunks.
    void iteratorInit(WifiIterator& it, const WifiList& list);
    int  iteratorNext(WifiIterator& it, char* outBuf, int bufLen);

    // Free SSID dictionary (call after a scan session is done).
    void wifiListCleanup();

private:
    std::map<std::string, int> m_ssidDict;
    std::mutex m_scanMutex;
    std::mutex m_dictMutex;

    static bool needUpdate(const std::map<std::string, int>& dict,
                           const char* ssid, int newRssi);
    int readWifiConfig(const char* interface, WifiCfg& cfg);
    int wifiConnect(const char* interface, const WifiCfg& cfg);
    std::string parseIwScanResults(const char* scanRes);
    void sortByRssi(WifiList& list);
};

} // namespace ft
