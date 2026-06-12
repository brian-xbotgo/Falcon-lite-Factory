#include "ble/BleDeviceInfo.h"
#include "ble/BleConstants.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

namespace {

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

std::string readWholeFile(const char* path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string shellQuote(const std::string& value)
{
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

std::string commandOutput(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {};
    }
    std::string out;
    char buf[128] = {};
    while (fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    pclose(pipe);
    return trim(out);
}

bool validSuffix(const std::string& value)
{
    if (value.size() != 6) {
        return false;
    }
    for (char ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

bool isDefaultFalconName(const char* name)
{
    return name && std::strcmp(name, "Xbt-F-000000") == 0;
}

std::string cpuSerial()
{
    std::string serial = trim(readWholeFile("/userdata/cpuinfo.txt"));
    if (!serial.empty()) {
        return serial;
    }

    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("Serial") == std::string::npos) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            return trim(line.substr(colon + 1));
        }
        std::istringstream ss(line);
        std::string token;
        std::string last;
        while (ss >> token) {
            last = token;
        }
        return trim(last);
    }
    return {};
}

std::string bleNameFromCpuSerial()
{
    const std::string serial = cpuSerial();
    if (serial.empty()) {
        return {};
    }
    const std::string suffix =
        commandOutput("printf %s " + shellQuote(serial) +
                      " | sha256sum | awk '{print $1}' | tail -c 6");
    if (!validSuffix(suffix)) {
        LOG("CPU serial hash suffix invalid: %s\n", suffix.c_str());
        return {};
    }
    return "Xbt-F-" + suffix;
}

} // namespace

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
        const auto fallback = bleNameFromCpuSerial();
        if (!fallback.empty()) {
            snprintf(m_bleName, sizeof(m_bleName), "%s", fallback.c_str());
            LOG("BLE name fallback from CPU serial: %s\n", m_bleName);
        }
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
        if (isDefaultFalconName(m_bleName)) {
            const auto fallback = bleNameFromCpuSerial();
            if (!fallback.empty()) {
                snprintf(m_bleName, sizeof(m_bleName), "%s", fallback.c_str());
                LOG("BLE name fallback from default SSID: %s\n", m_bleName);
            }
        }
    } else {
        LOG("SSID not found in %s, using default\n", SSID_FILE);
        const auto fallback = bleNameFromCpuSerial();
        if (!fallback.empty()) {
            snprintf(m_bleName, sizeof(m_bleName), "%s", fallback.c_str());
            LOG("BLE name fallback from CPU serial: %s\n", m_bleName);
        }
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
