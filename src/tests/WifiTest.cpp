#include "tests/ITestModule.h"
#include "platforms/common/interface/IWifiManager.h"
#include "config/PlatformConfig.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace ft {

namespace {

constexpr const char* kWifiSeparator = "--wifi--";

std::string trimPayload(std::string value)
{
    const auto nul = value.find('\0');
    if (nul != std::string::npos) {
        value.resize(nul);
    }
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' ||
            value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t pos = 0;
    while (pos < value.size() &&
           (value[pos] == '\n' || value[pos] == '\r' ||
            value[pos] == ' ' || value[pos] == '\t')) {
        ++pos;
    }
    if (pos > 0) {
        value.erase(0, pos);
    }
    return value;
}

std::string requestPayload(const TestContext& ctx)
{
    try {
        return ctx.params().value("request_payload", std::string());
    } catch (...) {
        return {};
    }
}

bool parseWifiConnectPayload(const std::string& payload,
                             std::string& ssid,
                             std::string& password)
{
    std::string text = trimPayload(payload);
    auto pos = text.find(kWifiSeparator);
    if (pos == std::string::npos && text.size() > 38) {
        text = trimPayload(text.substr(38));
        pos = text.find(kWifiSeparator);
    }
    if (pos == std::string::npos) {
        return false;
    }

    ssid = text.substr(0, pos);
    password = text.substr(pos + std::string(kWifiSeparator).size());
    return !ssid.empty();
}

std::string staInterface(const TestContext& ctx)
{
    try {
        const auto& root = ctx.config().raw();
        if (root.contains("wifi") && root.at("wifi").is_object()) {
            return root.at("wifi").value("sta_interface", "wlan0");
        }
    } catch (...) {
    }
    return "wlan0";
}

TestResult failWithCode(const std::string& detail)
{
    auto result = TestResult::fail(detail);
    result.data["error_code"] = 1;
    return result;
}

} // namespace

class WifiTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto wifi = ctx.create<IWifiManager>();
        if (!wifi)
            return TestResult::skipped("no wifi driver");

        const std::string topic = ctx.params().value("topic", std::string());
        if (topic == "37R") {
            std::string ssid;
            std::string password;
            const std::string payload = requestPayload(ctx);
            if (!parseWifiConnectPayload(payload, ssid, password)) {
                std::fprintf(stderr,
                             "[WifiTest] invalid 37R payload len=%zu\n",
                             payload.size());
                return failWithCode("wifi connect payload invalid");
            }

            nlohmann::json packed = {
                {"ssid", ssid},
                {"password", password},
                {"ip", ""},
            };
            const std::string packedText = packed.dump();
            std::fprintf(stderr, "[WifiTest] connect ssid=%s password_len=%zu\n",
                         ssid.c_str(), password.size());
            wifi->disconnectHotspot();
            const int rc = wifi->connectHotspot(
                reinterpret_cast<const uint8_t*>(packedText.data()),
                packedText.size());
            if (rc != 0) {
                std::fprintf(stderr, "[WifiTest] RK_wifi_connect failed rc=%d\n", rc);
                return failWithCode("wifi connect failed");
            }

            const std::string ifname = staInterface(ctx);
            for (int i = 0; i < 20; ++i) {
                const auto ip = wifi->getStaIp(ifname.c_str());
                if (!ip.empty() && ip != "0.0.0.0") {
                    std::fprintf(stderr, "[WifiTest] connected ssid=%s ip=%s\n",
                                 ssid.c_str(), ip.c_str());
                    auto result = TestResult::pass();
                    result.responseExtra = ip;
                    return result;
                }
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            return failWithCode("wifi ip timeout");
        }

        auto scan = wifi->scanWifi();
        std::fprintf(stderr, "[WifiTest] scan result_len=%zu\n", scan.size());
        if (scan.empty() || scan == "[]")
            return TestResult::fail("wifi scan empty");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("wifi", WifiTest);

} // namespace ft
