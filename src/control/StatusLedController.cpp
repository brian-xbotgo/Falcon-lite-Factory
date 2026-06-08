#include "control/StatusLedController.h"
#include "control/GpioController.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace ft {

namespace {

struct StatusLedState {
    int redGpio = -1;
    int whiteGpio = -1;
    std::atomic<bool> running{false};
    std::atomic<StatusLedMode> mode{StatusLedMode::Off};
    std::thread worker;
    mutable std::mutex lock;
};

StatusLedState& state()
{
    static StatusLedState s;
    return s;
}

bool sleepInterruptible(int ms)
{
    auto& s = state();
    const int stepMs = 20;
    int elapsed = 0;
    while (elapsed < ms && s.running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        elapsed += stepMs;
    }
    return s.running.load();
}

bool writeLed(int gpio, int value)
{
    if (gpio < 0) {
        return false;
    }
    const bool ok = GpioController::write(gpio, value);
    if (!ok) {
        std::fprintf(stderr, "[StatusLed] write gpio=%d value=%d result=FAIL\n",
                     gpio, value);
    }
    return ok;
}

void applyOff()
{
    auto& s = state();
    writeLed(s.redGpio, 0);
    writeLed(s.whiteGpio, 0);
}

void workerLoop()
{
    auto& s = state();
    while (s.running.load()) {
        const auto mode = s.mode.load();
        switch (mode) {
        case StatusLedMode::Normal:
            writeLed(s.redGpio, 0);
            writeLed(s.whiteGpio, 1);
            if (!sleepInterruptible(1000)) break;
            writeLed(s.whiteGpio, 0);
            writeLed(s.redGpio, 1);
            sleepInterruptible(1000);
            break;
        case StatusLedMode::Test:
            writeLed(s.redGpio, 0);
            writeLed(s.whiteGpio, 1);
            if (!sleepInterruptible(1000)) break;
            writeLed(s.whiteGpio, 0);
            sleepInterruptible(1000);
            break;
        case StatusLedMode::TestDone:
            writeLed(s.redGpio, 0);
            writeLed(s.whiteGpio, 1);
            sleepInterruptible(1000);
            break;
        case StatusLedMode::TestFail:
            writeLed(s.whiteGpio, 0);
            writeLed(s.redGpio, 1);
            if (!sleepInterruptible(1000)) break;
            writeLed(s.redGpio, 0);
            sleepInterruptible(1000);
            break;
        case StatusLedMode::Off:
        default:
            applyOff();
            sleepInterruptible(1000);
            break;
        }
    }
    applyOff();
}

} // namespace

StatusLedController& StatusLedController::instance()
{
    static StatusLedController controller;
    return controller;
}

const char* statusLedModeName(StatusLedMode mode)
{
    switch (mode) {
    case StatusLedMode::Off:      return "off";
    case StatusLedMode::Normal:   return "normal";
    case StatusLedMode::Test:     return "test";
    case StatusLedMode::TestDone: return "test_done";
    case StatusLedMode::TestFail: return "test_fail";
    }
    return "unknown";
}

bool StatusLedController::configure(const nlohmann::json& platformConfig)
{
    auto& s = state();
    std::lock_guard<std::mutex> guard(s.lock);

    try {
        const auto& led = platformConfig.at("gpio").at("led");
        s.whiteGpio = led.value("white", -1);
        s.redGpio = led.value("red", -1);
    } catch (...) {
        std::fprintf(stderr, "[StatusLed] config missing gpio.led.white/red\n");
        return false;
    }

    if (s.whiteGpio < 0 || s.redGpio < 0) {
        std::fprintf(stderr, "[StatusLed] invalid gpio red=%d white=%d\n",
                     s.redGpio, s.whiteGpio);
        return false;
    }

    bool ok = true;
    ok &= GpioController::exportGpio(s.redGpio);
    ok &= GpioController::setDirection(s.redGpio, "out");
    ok &= GpioController::write(s.redGpio, 0);
    ok &= GpioController::exportGpio(s.whiteGpio);
    ok &= GpioController::setDirection(s.whiteGpio, "out");
    ok &= GpioController::write(s.whiteGpio, 0);

    std::fprintf(stderr,
                 "[StatusLed] init red=%d white=%d result=%s active_high=1\n",
                 s.redGpio, s.whiteGpio, ok ? "PASS" : "FAIL");
    return ok;
}

bool StatusLedController::start(StatusLedMode mode)
{
    auto& s = state();
    std::lock_guard<std::mutex> guard(s.lock);

    if (s.redGpio < 0 || s.whiteGpio < 0) {
        std::fprintf(stderr, "[StatusLed] start failed: not configured\n");
        return false;
    }

    s.mode.store(mode);
    if (s.running.load()) {
        std::fprintf(stderr, "[StatusLed] mode=%s\n", statusLedModeName(mode));
        return true;
    }

    s.running.store(true);
    s.worker = std::thread(workerLoop);
    std::fprintf(stderr, "[StatusLed] thread started mode=%s\n",
                 statusLedModeName(mode));
    return true;
}

bool StatusLedController::setMode(StatusLedMode mode)
{
    auto& s = state();
    if (s.redGpio < 0 || s.whiteGpio < 0) {
        std::fprintf(stderr, "[StatusLed] set mode failed: not configured\n");
        return false;
    }
    s.mode.store(mode);
    if (!s.running.load()) {
        return start(mode);
    }
    std::fprintf(stderr, "[StatusLed] mode=%s\n", statusLedModeName(mode));
    return true;
}

void StatusLedController::stop()
{
    auto& s = state();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.running.load()) {
            applyOff();
            return;
        }
        s.running.store(false);
    }
    if (s.worker.joinable()) {
        s.worker.join();
    }
    std::fprintf(stderr, "[StatusLed] thread stopped\n");
}

bool StatusLedController::isConfigured() const
{
    auto& s = state();
    return s.redGpio >= 0 && s.whiteGpio >= 0;
}

} // namespace ft
