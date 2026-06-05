#pragma once

#include <cstdint>
#include <string>

namespace ft {

class BleDeviceInfo {
public:
    // Read device color from /device_data/info.ini (mainSn field).
    // Returns one of DEVICE_COLOR_* values.
    int getDeviceColor();

    // Read BLE name from /tmp/wps_hostapd.conf (ssid= line).
    const char* getBleName();

    // Load device alias from /userdata/device_alias.txt.
    void loadDeviceAlias();

    // Get device alias (falls back to BLE name if alias is empty).
    const char* getDeviceAlias();

private:
    bool m_bleNameInited = false;
    bool m_aliasInited = false;
    int  m_colorCached = -2;  // -2=uninit, -1=no file, >=0 cached

    char m_bleName[32] = "Xbt-F-000000";
    char m_deviceAlias[16] = "";  // MAX_ALIAS_LEN + 3
};

} // namespace ft
