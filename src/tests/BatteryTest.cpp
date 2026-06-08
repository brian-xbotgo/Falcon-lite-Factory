#include "tests/ITestModule.h"
#include "platforms/common/interface/IBatteryDriver.h"
#include <cstdio>

namespace ft {

namespace {

constexpr uint32_t kVoltageFail = 1u << 0;
constexpr uint32_t kInfoFail = 1u << 1;
constexpr double kMinBatteryVoltageV = 3.6;

TestResult failWithErrorCode(const std::string& detail, uint32_t errorCode)
{
    auto result = TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

} // namespace

class BatteryTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto bat = ctx.create<IBatteryDriver>();
        if (!bat)
            return failWithErrorCode("no battery driver", kInfoFail);

        const auto sysfsPath = bat->getSysfsPath();
        if (!bat->init()) {
            std::fprintf(stderr, "[BatteryTest] init failed sysfs=%s\n",
                         sysfsPath.empty() ? "<unset>" : sysfsPath.c_str());
            return failWithErrorCode("battery init failed", kInfoFail);
        }

        auto info = bat->read();
        if (!info.valid) {
            std::fprintf(stderr, "[BatteryTest] read invalid sysfs=%s\n",
                         sysfsPath.empty() ? "<unset>" : sysfsPath.c_str());
            return failWithErrorCode("battery read invalid", kInfoFail);
        }

        const bool voltagePass = info.voltage_v >= kMinBatteryVoltageV;
        std::fprintf(stderr,
                     "[BatteryTest] sysfs=%s model=%s voltage=%.3fV min=%.3fV percent=%d result=%s\n",
                     sysfsPath.empty() ? "<unset>" : sysfsPath.c_str(),
                     info.model.empty() ? "<unknown>" : info.model.c_str(),
                     info.voltage_v, kMinBatteryVoltageV, info.percent,
                     voltagePass ? "PASS" : "FAIL");

        auto result = voltagePass
            ? TestResult::pass()
            : TestResult::fail("battery voltage low");
        result.data["error_code"] = voltagePass ? 0 : kVoltageFail;
        result.data["voltage_v"] = info.voltage_v;
        result.data["percent"] = info.percent;
        result.data["model"] = info.model;
        return result;
    }
};

REGISTER_TEST_MODULE("battery", BatteryTest);

} // namespace ft
