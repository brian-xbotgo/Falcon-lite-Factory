#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include "control/I2cController.h"
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ft {

namespace {

constexpr uint32_t kOpenRtcFail = 1u << 0;
constexpr uint32_t kReadRtcFail = 1u << 1;
constexpr uint32_t kRtcYearFail = 1u << 2;
constexpr uint32_t kReadSystemTimeFail = 1u << 3;
constexpr uint32_t kConvertSystemTimeFail = 1u << 4;
constexpr uint32_t kCompareRtcTimeFail = 1u << 5;
constexpr uint32_t kI2cCommandFail = 1u << 6;
constexpr uint32_t kChargerIcFail = 1u << 7;
constexpr uint32_t kChargerReadFail = 1u << 8;
constexpr uint32_t kFuelGaugeIcFail = 1u << 9;
constexpr uint32_t kFuelGaugeReadFail = 1u << 10;
constexpr uint32_t kFuelGaugeDevFail = 1u << 11;

int parseIntValue(const nlohmann::json& value, int fallback)
{
    try {
        if (value.is_number_integer()) {
            return value.get<int>();
        }
        if (value.is_string()) {
            return std::stoi(value.get<std::string>(), nullptr, 0);
        }
    } catch (...) {
    }
    return fallback;
}

int shiftForMask(int mask)
{
    if (mask == 0) {
        return 0;
    }

    int shift = 0;
    while (((mask >> shift) & 0x1) == 0 && shift < 31) {
        ++shift;
    }
    return shift;
}

bool probeI2cRegister(const nlohmann::json& root,
                      const char* section,
                      uint32_t readFailBit,
                      uint32_t checkFailBit,
                      uint32_t& errorCode)
{
    try {
        const auto& cfg = root.at("i2c").at(section);
        const int bus = parseIntValue(cfg.at("bus"), -1);
        const int addr = parseIntValue(cfg.at("addr"), -1);
        const int reg = parseIntValue(cfg.at("probe_reg"), -1);
        const int expect = cfg.contains("probe_expect")
            ? parseIntValue(cfg.at("probe_expect"), -1)
            : -1;
        const int mask = cfg.contains("probe_mask") ? parseIntValue(cfg.at("probe_mask"), 0) : 0;
        std::vector<int> expectedValues;
        if (cfg.contains("probe_expect_any") && cfg.at("probe_expect_any").is_array()) {
            for (const auto& item : cfg.at("probe_expect_any")) {
                const int parsed = parseIntValue(item, -1);
                if (parsed >= 0) {
                    expectedValues.push_back(parsed);
                }
            }
        } else if (expect >= 0) {
            expectedValues.push_back(expect);
        }

        if (bus < 0 || addr < 0 || reg < 0 || expectedValues.empty()) {
            std::fprintf(stderr,
                         "[RtcTest] %s config invalid bus=%d addr=0x%x reg=0x%x expect_count=%zu\n",
                         section, bus, addr, reg, expectedValues.size());
            errorCode |= readFailBit;
            return false;
        }

        uint8_t value = 0;
        if (!I2cController::readByte(bus, addr, static_cast<uint8_t>(reg), value)) {
            std::fprintf(stderr,
                         "[RtcTest] %s read failed bus=%d addr=0x%02x reg=0x%02x\n",
                         section, bus, addr, reg);
            errorCode |= kI2cCommandFail | readFailBit;
            return false;
        }

        const int actual = mask ? ((value & mask) >> shiftForMask(mask)) : value;
        bool pass = false;
        for (int expected : expectedValues) {
            if (actual == expected) {
                pass = true;
                break;
            }
        }
        std::string expectText;
        for (size_t i = 0; i < expectedValues.size(); ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%s0x%x", i == 0 ? "" : "/", expectedValues[i]);
            expectText += buf;
        }
        std::fprintf(stderr,
                     "[RtcTest] %s bus=%d addr=0x%02x reg=0x%02x value=0x%02x mask=0x%02x actual=0x%x expect=%s result=%s\n",
                     section, bus, addr, reg, value, mask, actual,
                     expectText.c_str(),
                     pass ? "PASS" : "FAIL");

        if (!pass) {
            errorCode |= checkFailBit;
        }
        return pass;
    } catch (...) {
        std::fprintf(stderr, "[RtcTest] %s config missing or invalid\n", section);
        errorCode |= readFailBit;
        return false;
    }
}

TestResult resultFromErrorCode(uint32_t errorCode)
{
    TestResult result = errorCode == 0
        ? TestResult::pass()
        : TestResult::fail("rtc/ic test failed");
    result.data["error_code"] = errorCode;
    return result;
}

} // namespace

class RtcTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        uint32_t errorCode = 0;
        rtc_time rtcTm {};

        int fd = ::open("/dev/rtc", O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr, "[RtcTest] open /dev/rtc failed\n");
            errorCode |= kOpenRtcFail;
        } else {
            if (::ioctl(fd, RTC_RD_TIME, &rtcTm) < 0) {
                std::fprintf(stderr, "[RtcTest] read /dev/rtc failed\n");
                errorCode |= kReadRtcFail;
            }
            ::close(fd);
        }

        if ((errorCode & kReadRtcFail) == 0 && (errorCode & kOpenRtcFail) == 0) {
            const int rtcYear = rtcTm.tm_year + 1900;
            std::fprintf(stderr,
                         "[RtcTest] rtc_time=%04d-%02d-%02d %02d:%02d:%02d min_year=2026\n",
                         rtcYear, rtcTm.tm_mon + 1, rtcTm.tm_mday,
                         rtcTm.tm_hour, rtcTm.tm_min, rtcTm.tm_sec);
            if (rtcYear < 2026) {
                errorCode |= kRtcYearFail;
            }
        }

        const std::time_t sysTime = std::time(nullptr);
        std::tm sysTm {};
        if (sysTime == static_cast<std::time_t>(-1)) {
            std::fprintf(stderr, "[RtcTest] read system time failed\n");
            errorCode |= kReadSystemTimeFail;
        } else if (!::localtime_r(&sysTime, &sysTm)) {
            std::fprintf(stderr, "[RtcTest] convert system time failed\n");
            errorCode |= kConvertSystemTimeFail;
        } else {
            std::fprintf(stderr,
                         "[RtcTest] sys_time=%04d-%02d-%02d %02d:%02d:%02d\n",
                         sysTm.tm_year + 1900, sysTm.tm_mon + 1, sysTm.tm_mday,
                         sysTm.tm_hour, sysTm.tm_min, sysTm.tm_sec);
        }

        if ((errorCode & (kOpenRtcFail | kReadRtcFail |
                          kReadSystemTimeFail | kConvertSystemTimeFail)) == 0) {
            const bool sameHour = rtcTm.tm_year == sysTm.tm_year &&
                                  rtcTm.tm_mon == sysTm.tm_mon &&
                                  rtcTm.tm_mday == sysTm.tm_mday &&
                                  rtcTm.tm_hour == sysTm.tm_hour;
            std::fprintf(stderr,
                         "[RtcTest] compare rtc/system same year-month-day-hour result=%s\n",
                         sameHour ? "PASS" : "FAIL");
            if (!sameHour) {
                errorCode |= kCompareRtcTimeFail;
            }
        }

        const auto& root = ctx.config().raw();
        probeI2cRegister(root, "charger", kChargerReadFail, kChargerIcFail, errorCode);
        probeI2cRegister(root, "fuel_gauge", kFuelGaugeReadFail, kFuelGaugeIcFail, errorCode);

        std::string fuelDev;
        try {
            fuelDev = root.at("battery").value("dev_node", std::string());
        } catch (...) {
        }
        if (!fuelDev.empty()) {
            const bool readable = ::access(fuelDev.c_str(), R_OK) == 0;
            std::fprintf(stderr, "[RtcTest] fuel_gauge_dev=%s readable=%d\n",
                         fuelDev.c_str(), readable ? 1 : 0);
            if (!readable) {
                errorCode |= kFuelGaugeDevFail;
            }
        }

        std::fprintf(stderr, "[RtcTest] final error_code=0x%08x\n", errorCode);
        return resultFromErrorCode(errorCode);
    }
};

REGISTER_TEST_MODULE("rtc", RtcTest);

} // namespace ft
