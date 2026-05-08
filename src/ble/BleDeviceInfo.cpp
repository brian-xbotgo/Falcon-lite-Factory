#include "ble/BleDeviceInfo.h"
#include "ble/BleConstants.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

int BleDeviceInfo::getDeviceColor()
{
    if (m_colorCached != -2) {
        if (m_colorCached == -1) return DEVICE_COLOR_DARK_GREY;
        return m_colorCached;
    }

    FILE* f = fopen(INFO_INI_PATH, "r");
    if (!f) {
        LOG("Failed to open %s\n", INFO_INI_PATH);
        m_colorCached = -1;
        return DEVICE_COLOR_DARK_GREY;
    }

    char mainSn[64] = {};
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Parse: mainSn:xxx  or  mainSn = xxx
        char key[32] = {}, value[64] = {};
        if (sscanf(line, "%31[^=:]%*1[:=]%63s", key, value) == 2) {
            // trim trailing whitespace from key
            for (int i = strlen(key) - 1; i >= 0 && key[i] == ' '; i--) key[i] = '\0';
            if (strcmp(key, "mainSn") == 0) {
                strncpy(mainSn, value, sizeof(mainSn) - 1);
                break;
            }
        }
    }
    fclose(f);

    if (mainSn[0] == '\0') {
        LOG("mainSn not found in %s\n", INFO_INI_PATH);
        m_colorCached = -1;
        return DEVICE_COLOR_DARK_GREY;
    }

    LOG("mainSn: %s\n", mainSn);

    if (strlen(mainSn) < 4) {
        LOG("mainSn too short: %s\n", mainSn);
        m_colorCached = -1;
        return DEVICE_COLOR_DARK_GREY;
    }

    char colorCode[4] = {};
    strncpy(colorCode, mainSn + 1, 3);  // chars 2-4 (index 1-3)

    int color = DEVICE_COLOR_DARK_GREY;
    if (strcmp(colorCode, "041") == 0)
        color = DEVICE_COLOR_GREEN;
    else if (strcmp(colorCode, "042") == 0)
        color = DEVICE_COLOR_PINK;
    else if (strcmp(colorCode, "043") == 0)
        color = DEVICE_COLOR_DARK_GREY;
    else
        LOG("Unknown color code: %s\n", colorCode);

    m_colorCached = color;
    return color;
}

const char* BleDeviceInfo::getBleName()
{
    if (m_bleNameInited) return m_bleName;
    m_bleNameInited = true;

    FILE* fp = fopen(SSID_FILE, "r");
    if (!fp) {
        LOG("Failed to open %s\n", SSID_FILE);
        return m_bleName;
    }

    char line[256];
    char ssid[64] = {};
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ssid=", 5) == 0) {
            sscanf(line, "ssid=%63s", ssid);
            break;
        }
    }
    fclose(fp);

    if (ssid[0]) {
        snprintf(m_bleName, sizeof(m_bleName), "%s", ssid);
    } else {
        LOG("SSID not found in %s, using default\n", SSID_FILE);
    }
    return m_bleName;
}

void BleDeviceInfo::loadDeviceAlias()
{
    FILE* fp = fopen(DEVICE_ALIAS_FILE, "r");
    if (!fp) {
        m_deviceAlias[0] = '\0';
        return;
    }
    if (!fgets(m_deviceAlias, sizeof(m_deviceAlias), fp)) {
        m_deviceAlias[0] = '\0';
    } else {
        size_t len = strlen(m_deviceAlias);
        if (len > 0 && m_deviceAlias[len - 1] == '\n')
            m_deviceAlias[len - 1] = '\0';
    }
    fclose(fp);
    m_aliasInited = true;
    LOG("Device alias: %s\n", m_deviceAlias);
}

const char* BleDeviceInfo::getDeviceAlias()
{
    if (!m_aliasInited) loadDeviceAlias();
    if (strlen(m_deviceAlias) > 0) return m_deviceAlias;
    return getBleName();
}

} // namespace ft
