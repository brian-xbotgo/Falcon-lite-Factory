#include "FalconRuntime.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace ft::falcon_runtime {

namespace {

constexpr const char* kLogPrefix = "[FalconRuntime]";

int runShell(const std::string& cmd)
{
    const int rc = std::system(cmd.c_str());
    if (rc == -1) {
        return -1;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return rc;
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

std::string readCommandOutput(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {};
    }

    std::string out;
    char buffer[128] = {};
    while (fgets(buffer, sizeof(buffer), pipe)) {
        out += buffer;
    }
    pclose(pipe);
    return trim(out);
}

bool fileExists(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

bool dirExists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void ensureDirs()
{
    mkdir("/userdata", 0755);
    mkdir("/userdata/logs", 0755);
    mkdir("/userdata/record", 0755);
    mkdir("/tmp", 0777);
    mkdir("/var/run", 0755);
    mkdir("/var/run/factory_fw", 0755);
}

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeWholeFile(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "%s write failed path=%s err=%s\n",
                     kLogPrefix, path.c_str(), std::strerror(errno));
        return false;
    }
    out << text;
    return out.good();
}

std::string cpuSerialFromProc()
{
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

bool ensureCpuInfoFile(std::string& serialOut)
{
    const std::string procSerial = cpuSerialFromProc();
    const std::string path = "/userdata/cpuinfo.txt";

    if (!procSerial.empty()) {
        serialOut = procSerial;
        const std::string existing = trim(readWholeFile(path));
        if (existing != procSerial) {
            return writeWholeFile(path, procSerial + "\n");
        }
        return true;
    }

    serialOut = trim(readWholeFile(path));
    if (!serialOut.empty()) {
        std::fprintf(stderr, "%s /proc/cpuinfo Serial missing, reuse %s\n",
                     kLogPrefix, path.c_str());
        return true;
    }

    std::fprintf(stderr, "%s CPU serial missing; keep default SSID\n", kLogPrefix);
    return false;
}

bool validHashSuffix(const std::string& value)
{
    if (value.size() != 6) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::string defaultHostapdTemplate()
{
    return
        "driver=nl80211\n"
        "interface=wlan1\n"
        "ssid=Xbt-F-000000\n"
        "wpa=3\n"
        "wpa_key_mgmt=WPA-PSK\n"
        "wpa_pairwise=CCMP\n"
        "wpa_passphrase=cd20d767bbe\n"
        "rsn_pairwise=CCMP\n";
}

std::string hostapdTemplate()
{
    for (const char* path : {
             "/oem/usr/conf/wps_hostapd.conf",
             "/tmp/wps_hostapd.conf",
             "/etc/wps_hostapd.conf",
         }) {
        const std::string text = readWholeFile(path);
        if (!text.empty()) {
            return text;
        }
    }
    return defaultHostapdTemplate();
}

std::string replaceOrAppendLine(const std::string& text,
                                const std::string& key,
                                const std::string& value)
{
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) == 0) {
            out << key << value << '\n';
            found = true;
        } else {
            out << line << '\n';
        }
    }
    if (!found) {
        out << key << value << '\n';
    }
    return out.str();
}

std::string iqDir()
{
    if (dirExists("/oem/usr/iqfiles")) {
        return "/oem/usr/iqfiles";
    }
    if (dirExists("/etc/iqfiles")) {
        return "/etc/iqfiles";
    }
    return "/oem/usr/iqfiles";
}

std::string rkaiqBinary()
{
    for (const char* path : {
             "/oem/usr/bin/rkaiq_3A_server",
             "/usr/bin/rkaiq_3A_server",
         }) {
        if (fileExists(path)) {
            return path;
        }
    }
    return {};
}

void cleanupRkaiqIpc()
{
    runShell("rm -f /tmp/aiq0.lock /tmp/aiq1.lock /tmp/.rkaiq_3A "
             "/tmp/rkaiq_* /tmp/*.rkaiq /var/tmp/rkipc 2>/dev/null");
    runShell("ipcrm -a >/dev/null 2>&1");
}

std::vector<std::string> recorderDevicesFromJson(const std::string& path)
{
    std::vector<std::string> devices;
    std::ifstream in(path);
    if (!in.is_open()) {
        return devices;
    }

    try {
        nlohmann::json root;
        in >> root;
        const auto cameras = root.value("cameras", nlohmann::json::array());
        if (cameras.is_array()) {
            for (const auto& item : cameras) {
                if (!item.is_object()) {
                    continue;
                }
                const std::string device = item.value("device", std::string());
                if (!device.empty()) {
                    devices.push_back(device);
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s recorder config parse failed path=%s err=%s\n",
                     kLogPrefix, path.c_str(), e.what());
    }
    return devices;
}

std::vector<std::string> recorderDevicesFromPlatform(const nlohmann::json& config)
{
    std::string path = "/oem/usr/conf/recorder.json";
    try {
        path = config.at("video").value("recorder_config", path);
    } catch (...) {
    }

    auto devices = recorderDevicesFromJson(path);
    if (devices.empty()) {
        devices = {"/dev/video22", "/dev/video31"};
    }
    return devices;
}

bool videoNodesExist(const std::vector<std::string>& devices)
{
    for (const auto& device : devices) {
        if (!fileExists(device)) {
            return false;
        }
    }
    return true;
}

bool rkaiqLogHasFatalError()
{
    const std::string log = readWholeFile("/userdata/logs/rkaiq_3A_server.log");
    return log.find("_rkAiqManager init error") != std::string::npos ||
           log.find("Segmentation fault") != std::string::npos;
}

bool rkaiqProcessRunning()
{
    return runShell("pidof rkaiq_3A_server >/dev/null 2>&1") == 0;
}

bool waitReady(const std::vector<std::string>& devices, int timeoutMs)
{
    const int boundedTimeout = std::max(0, timeoutMs);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(boundedTimeout);
    int stableChecks = 0;

    do {
        if (rkaiqLogHasFatalError()) {
            return false;
        }
        if (rkaiqProcessRunning() && videoNodesExist(devices)) {
            ++stableChecks;
        } else {
            stableChecks = 0;
        }
        if (stableChecks >= 8) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (std::chrono::steady_clock::now() < deadline);

    return !rkaiqLogHasFatalError() && rkaiqProcessRunning() &&
           videoNodesExist(devices) && stableChecks > 0;
}

void logRkaiqState(const std::vector<std::string>& devices)
{
    const bool running = rkaiqProcessRunning();
    std::fprintf(stderr, "%s rkaiq state running=%d", kLogPrefix, running ? 1 : 0);
    for (const auto& device : devices) {
        std::fprintf(stderr, " %s=%d", device.c_str(), fileExists(device) ? 1 : 0);
    }
    std::fprintf(stderr, "\n");
}

} // namespace

void prepareWifiIdentity()
{
    ensureDirs();

    std::string serial;
    if (!ensureCpuInfoFile(serial)) {
        return;
    }

    const std::string suffix = readCommandOutput(
        "tr -d '\\n\\r' < /userdata/cpuinfo.txt | sha256sum | awk '{print $1}' | tail -c 6");
    if (!validHashSuffix(suffix)) {
        std::fprintf(stderr, "%s CPU hash suffix invalid value=%s\n",
                     kLogPrefix, suffix.c_str());
        return;
    }

    const std::string ssid = "Xbt-F-" + suffix;
    const std::string password = readCommandOutput(
        "printf %s " + shellQuote(ssid + "DragonflySalt") +
        " | sha256sum | cut -c1-11");
    if (password.size() != 11) {
        std::fprintf(stderr, "%s Wi-Fi password hash invalid ssid=%s\n",
                     kLogPrefix, ssid.c_str());
        return;
    }

    std::string conf = hostapdTemplate();
    conf = replaceOrAppendLine(conf, "ssid=", ssid);
    conf = replaceOrAppendLine(conf, "wpa_passphrase=", password);
    if (writeWholeFile("/tmp/wps_hostapd.conf", conf)) {
        std::fprintf(stderr, "%s Wi-Fi/BLE identity ssid=%s cpu_serial_len=%zu\n",
                     kLogPrefix, ssid.c_str(), serial.size());
    }
}

bool ensureRkaiqStarted(const nlohmann::json& config, int timeoutMs)
{
    ensureDirs();

    const auto devices = recorderDevicesFromPlatform(config);
    const std::string binary = rkaiqBinary();
    if (binary.empty()) {
        std::fprintf(stderr, "%s rkaiq_3A_server not found\n", kLogPrefix);
        return false;
    }

    const std::string iq = iqDir();
    if (rkaiqProcessRunning()) {
        if (waitReady(devices, 2000)) {
            std::fprintf(stderr, "%s reuse running rkaiq_3A_server iq_dir=%s\n",
                         kLogPrefix, iq.c_str());
            return true;
        }
        logRkaiqState(devices);
        std::fprintf(stderr, "%s keep running rkaiq_3A_server for diagnostics iq_dir=%s\n",
                     kLogPrefix, iq.c_str());
        return false;
    }

    cleanupRkaiqIpc();
    writeWholeFile("/userdata/logs/rkaiq_3A_server.log", "");
    const std::string cmd = shellQuote(binary) + " -a " + shellQuote(iq) +
        " >/userdata/logs/rkaiq_3A_server.log 2>&1 < /dev/null &";
    std::fprintf(stderr, "%s start rkaiq cmd=%s\n", kLogPrefix, cmd.c_str());
    runShell(cmd);

    if (waitReady(devices, timeoutMs)) {
        std::fprintf(stderr, "%s rkaiq ready iq_dir=%s\n", kLogPrefix, iq.c_str());
        return true;
    }

    std::fprintf(stderr, "%s rkaiq readiness timed out iq_dir=%s devices=%zu\n",
                 kLogPrefix, iq.c_str(), devices.size());
    return false;
}

bool waitRkaiqReady(const RecorderConfig& config, int timeoutMs)
{
    std::vector<std::string> devices;
    for (const auto& camera : config.cameras) {
        if (!camera.device.empty()) {
            devices.push_back(camera.device);
        }
    }
    if (devices.empty()) {
        devices = {"/dev/video22", "/dev/video31"};
    }

    const bool ok = waitReady(devices, timeoutMs);
    if (!ok) {
        std::fprintf(stderr, "%s rkaiq not ready for recorder devices=%zu\n",
                     kLogPrefix, devices.size());
    }
    return ok;
}

void prepareStartup(const nlohmann::json& config)
{
    prepareWifiIdentity();
    if (!rkaiqProcessRunning()) {
        ensureRkaiqStarted(config, 15000);
    }
}

} // namespace ft::falcon_runtime
