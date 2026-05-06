// Wifi test: 16R (scan) / 37R (connect)
// 16R: wpa_cli scan + scan_res check
// 37R: non-standard format "SSID--wifi--PASSWORD", response is IP string

#include "tests/WifiTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

namespace ft {

static bool check_wpa_cli_rescan_failed() {
    auto r = shell_exec("wpa_cli scan_res 2>&1");
    if (r.output.find("Failed") != std::string::npos) {
        std::fprintf(stderr, "[wifi] Failed to rescan\n");
        return true;
    }
    std::fprintf(stderr, "[wifi] Rescan success\n");
    return false;
}

void register_wifi_tests(TestEngine& engine) {

    // 16R: WiFi scan
    engine.registerTest("16R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;

        std::system("wpa_cli scan > /dev/null");
        if (check_wpa_cli_rescan_failed()) {
            err |= 0x0001;
        }

        std::fprintf(stderr, "[16R] wifi scan, sn=%s, err=0x%08X\n", hdr.sn, err);
        return err;
    });

    // 37R: WiFi connect (custom format)
    engine.registerRaw("37R", [&engine](const uint8_t* data, size_t len) {
        std::string payload(reinterpret_cast<const char*>(data), len);

        const std::string delimiter = "--wifi--";
        auto pos = payload.find(delimiter);
        if (pos == std::string::npos) {
            std::fprintf(stderr, "[37R] bad wifi payload: %s\n", payload.c_str());
            engine.publish("37A", "ERROR", 2);
            return;
        }

        std::string ssid     = payload.substr(0, pos);
        std::string password = payload.substr(pos + delimiter.size());
        std::fprintf(stderr, "[37R] connecting to SSID=%s\n", ssid.c_str());

        std::string cmd = "wifi-connect.sh '" + ssid + "' '" + password + "'";
        shell_exec(cmd);

        std::string ip;
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            auto r = shell_exec("wpa_cli status");
            auto ip_pos = r.output.find("ip_address=");
            if (ip_pos != std::string::npos) {
                auto start = ip_pos + 11;
                auto end   = r.output.find('\n', start);
                ip = r.output.substr(start, end - start);
                if (!ip.empty() && ip != "0.0.0.0") break;
                ip.clear();
            }
        }

        if (ip.empty()) {
            std::fprintf(stderr, "[37R] wifi connect failed\n");
            engine.publish("37A", "ERROR", 2);
        } else {
            std::fprintf(stderr, "[37R] connected, ip=%s\n", ip.c_str());
            engine.publish("37A", ip, 2);
        }
    }, /*async=*/true);
}

} // namespace ft
