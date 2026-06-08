#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include "platforms/common/interface/IGpioDriver.h"
#include "platforms/common/interface/IHallDriver.h"
#include "platforms/common/interface/IMotorDriver.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>

namespace ft {

namespace {

constexpr uint32_t kHallInitFail = 1u << 0;
constexpr uint32_t kUpperLimitFail = 1u << 0;
constexpr uint32_t kLowerLimitFail = 1u << 1;
constexpr uint32_t kHallReadFail = 1u << 1;
constexpr uint32_t kHallMaxVoltageFail = 1u << 2;
constexpr uint32_t kHallMinVoltageFail = 1u << 3;

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

float parseFloatValue(const nlohmann::json& value, float fallback)
{
    try {
        if (value.is_number()) {
            return value.get<float>();
        }
        if (value.is_string()) {
            return std::stof(value.get<std::string>());
        }
    } catch (...) {
    }
    return fallback;
}

float hallConfigFloat(const nlohmann::json& root, const char* key, float fallback)
{
    try {
        return parseFloatValue(root.at("i2c").at("hall").at(key), fallback);
    } catch (...) {
        return fallback;
    }
}

int hallConfigInt(const nlohmann::json& root, const char* key, int fallback)
{
    try {
        return parseIntValue(root.at("i2c").at("hall").at(key), fallback);
    } catch (...) {
        return fallback;
    }
}

TestResult resultWithCode(bool pass, uint32_t errorCode, const std::string& detail)
{
    auto result = pass ? TestResult::pass() : TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

MotorDirect parseDirect(const std::string& value)
{
    if (value == "backward" || value == "back") {
        return DIRECT_BACKWARD;
    }
    return DIRECT_FORWARD;
}

const char* directName(MotorDirect direct)
{
    return direct == DIRECT_FORWARD ? "forward" : "backward";
}

int readLimitValue(IGpioDriver& gpio, int gpioId)
{
    if (gpioId < 0) {
        return -1;
    }
    if (!gpio.exportGpio(gpioId) || !gpio.setDirection(gpioId, "in")) {
        return -1;
    }
    return gpio.read(gpioId);
}

bool waitLimitActive(IGpioDriver& gpio, int gpioId, int activeLevel, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, timeoutMs));
    int lastValue = -1;

    while (std::chrono::steady_clock::now() < deadline) {
        lastValue = readLimitValue(gpio, gpioId);
        if (lastValue == activeLevel) {
            std::fprintf(stderr,
                         "[HallTest] limit active gpio=%d value=%d active_level=%d\n",
                         gpioId, lastValue, activeLevel);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::fprintf(stderr,
                 "[HallTest] limit timeout gpio=%d last_value=%d active_level=%d timeout_ms=%d\n",
                 gpioId, lastValue, activeLevel, timeoutMs);
    return false;
}

} // namespace

class HallTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const auto mode = ctx.params().value("mode", std::string("horizontal_voltage"));
        if (mode == "vertical_limit") {
            return runVerticalLimit(ctx);
        }
        return runHorizontalVoltage(ctx);
    }

private:
    TestResult runVerticalLimit(TestContext& ctx) {
        return runVerticalLimitGpio(ctx);
    }

    TestResult runVerticalLimitGpio(TestContext& ctx) {
        const auto& root = ctx.config().raw();
        int upperGpio = -1;
        int lowerGpio = -1;
        try {
            const auto& hallGpio = root.at("gpio").at("hall");
            upperGpio = parseIntValue(hallGpio.at("upper_limit"), -1);
            lowerGpio = parseIntValue(hallGpio.at("lower_limit"), -1);
        } catch (...) {
        }

        uint32_t errorCode = 0;
        if (upperGpio < 0) {
            errorCode |= kUpperLimitFail;
        }
        if (lowerGpio < 0) {
            errorCode |= kLowerLimitFail;
        }

        auto gpio = ctx.create<IGpioDriver>();
        if (!gpio) {
            std::fprintf(stderr, "[HallTest] vertical_limit no gpio driver\n");
            return resultWithCode(false, kUpperLimitFail | kLowerLimitFail,
                                  "no gpio driver");
        }

        const int activeLevel = ctx.params().value("active_level", 0);
        const int timeoutMs = ctx.params().value("timeout_ms", 10000);
        const float travelAngle = ctx.params().value("travel_angle", 420.0f);
        const float centerAngle = ctx.params().value("center_angle", 75.0f);
        const auto lowerDirect = parseDirect(ctx.params().value("lower_direct",
                                                                std::string("forward")));
        const auto upperDirect = parseDirect(ctx.params().value("upper_direct",
                                                                std::string("backward")));

        int upperValue = readLimitValue(*gpio, upperGpio);
        int lowerValue = readLimitValue(*gpio, lowerGpio);
        if (upperValue < 0) {
            errorCode |= kUpperLimitFail;
        }
        if (lowerValue < 0) {
            errorCode |= kLowerLimitFail;
        }

        std::fprintf(stderr,
                     "[HallTest] vertical_limit start upper_gpio=%d upper_value=%d lower_gpio=%d lower_value=%d active_level=%d timeout_ms=%d travel=%.1f center=%.1f\n",
                     upperGpio, upperValue, lowerGpio, lowerValue,
                     activeLevel, timeoutMs, travelAngle, centerAngle);

        auto motor = ctx.create<IMotorDriver>();
        if (!motor || !motor->init()) {
            std::fprintf(stderr, "[HallTest] vertical_limit motor init failed\n");
            return resultWithCode(false,
                                  errorCode | kUpperLimitFail | kLowerLimitFail,
                                  "motor init failed");
        }

        if ((errorCode & kLowerLimitFail) == 0 && lowerValue != activeLevel) {
            const bool started = motor->move(MOTOR_VERTICAL, travelAngle,
                                            SPEED_MID, lowerDirect);
            std::fprintf(stderr,
                         "[HallTest] vertical_limit move lower direct=%s angle=%.1f started=%d\n",
                         directName(lowerDirect), travelAngle, started ? 1 : 0);
            if (!started || !waitLimitActive(*gpio, lowerGpio, activeLevel, timeoutMs)) {
                errorCode |= kLowerLimitFail;
            }
            motor->stop(MOTOR_VERTICAL);
        }

        if ((errorCode & kUpperLimitFail) == 0) {
            upperValue = readLimitValue(*gpio, upperGpio);
            if (upperValue != activeLevel) {
                const bool started = motor->move(MOTOR_VERTICAL, travelAngle,
                                                SPEED_MID, upperDirect);
                std::fprintf(stderr,
                             "[HallTest] vertical_limit move upper direct=%s angle=%.1f started=%d\n",
                             directName(upperDirect), travelAngle, started ? 1 : 0);
                if (!started || !waitLimitActive(*gpio, upperGpio, activeLevel, timeoutMs)) {
                    errorCode |= kUpperLimitFail;
                }
                motor->stop(MOTOR_VERTICAL);
            }
        }

        if (errorCode == 0 && centerAngle > 0.0f) {
            const bool started = motor->move(MOTOR_VERTICAL, centerAngle,
                                            SPEED_MID, lowerDirect);
            std::fprintf(stderr,
                         "[HallTest] vertical_limit center direct=%s angle=%.1f started=%d\n",
                         directName(lowerDirect), centerAngle, started ? 1 : 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            motor->stop(MOTOR_VERTICAL);
        }

        upperValue = readLimitValue(*gpio, upperGpio);
        lowerValue = readLimitValue(*gpio, lowerGpio);
        motor->deinit();

        std::fprintf(stderr,
                     "[HallTest] vertical_limit final upper_gpio=%d upper_value=%d lower_gpio=%d lower_value=%d error_code=0x%08x result=%s\n",
                     upperGpio, upperValue, lowerGpio, lowerValue,
                     errorCode, errorCode == 0 ? "PASS" : "FAIL");
        return resultWithCode(errorCode == 0, errorCode,
                              errorCode == 0 ? "" : "hall limit gpio failed");
    }

    TestResult runHorizontalVoltage(TestContext& ctx) {
        auto hall = ctx.create<IHallDriver>();
        if (!hall) {
            return resultWithCode(false, kHallInitFail, "no hall driver");
        }
        if (!hall->init()) {
            return resultWithCode(false, kHallInitFail, "hall init failed");
        }

        const auto& root = ctx.config().raw();
        const int sampleCount = ctx.params().value(
            "sample_count", hallConfigInt(root, "sample_count", 120));
        const int sampleIntervalMs = ctx.params().value(
            "sample_interval_ms", hallConfigInt(root, "sample_interval_ms", 50));
        const float minVoltageExpect = hallConfigFloat(root, "min_voltage", 1.6f);
        const float maxVoltageExpect = hallConfigFloat(root, "max_voltage", 2.0f);
        const float moveAngle = ctx.params().value("move_angle", 420.0f);
        const auto direct = parseDirect(ctx.params().value("move_direct",
                                                           std::string("forward")));

        auto motor = ctx.create<IMotorDriver>();
        bool motorStarted = false;
        if (motor && motor->init()) {
            motorStarted = motor->move(MOTOR_HORIZONTAL, moveAngle, SPEED_MID, direct);
            std::fprintf(stderr,
                         "[HallTest] horizontal_voltage motor angle=%.1f direct=%s started=%d\n",
                         moveAngle, directName(direct), motorStarted ? 1 : 0);
        } else {
            std::fprintf(stderr,
                         "[HallTest] horizontal_voltage motor unavailable, sampling static voltage\n");
        }

        float minVoltage = std::numeric_limits<float>::max();
        float maxVoltage = std::numeric_limits<float>::lowest();
        int validSamples = 0;
        const int boundedSamples = std::max(1, sampleCount);
        const int boundedIntervalMs = std::max(0, sampleIntervalMs);

        for (int i = 0; i < boundedSamples; ++i) {
            const float value = hall->readValue();
            if (value < 1000.0f) {
                minVoltage = std::min(minVoltage, value);
                maxVoltage = std::max(maxVoltage, value);
                ++validSamples;
            }
            if (boundedIntervalMs > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(boundedIntervalMs));
            }
        }

        if (motor && motor->isInitialized()) {
            motor->stop(MOTOR_HORIZONTAL);
            motor->deinit();
        }
        hall->deinit();

        uint32_t errorCode = 0;
        if (validSamples == 0) {
            errorCode |= kHallReadFail;
            minVoltage = 0.0f;
            maxVoltage = 0.0f;
        } else {
            if (maxVoltage < maxVoltageExpect) {
                errorCode |= kHallMaxVoltageFail;
            }
            if (minVoltage < minVoltageExpect) {
                errorCode |= kHallMinVoltageFail;
            }
        }

        std::fprintf(stderr,
                     "[HallTest] horizontal_voltage samples=%d/%d min=%.4fV expect_min>=%.4fV max=%.4fV expect_max>=%.4fV motor_started=%d result=%s error_code=0x%08x\n",
                     validSamples, boundedSamples,
                     minVoltage, minVoltageExpect,
                     maxVoltage, maxVoltageExpect,
                     motorStarted ? 1 : 0,
                     errorCode == 0 ? "PASS" : "FAIL",
                     errorCode);

        return resultWithCode(errorCode == 0, errorCode,
                              errorCode == 0 ? "" : "hall voltage failed");
    }
};

REGISTER_TEST_MODULE("hall", HallTest);

} // namespace ft
