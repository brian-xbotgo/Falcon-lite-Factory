#include "config/PlatformConfig.h"
#include "core/DriverRegistry.h"
#include "core/TestEngine.h"
#include "control/StatusLedController.h"
#include "platforms/common/interface/IPlatform.h"
#include "platforms/common/interface/IDisplayDriver.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <csignal>

using namespace ft;

namespace {

std::atomic<bool> gQuit{false};

void handleSignal(int)
{
    gQuit = true;
}

} // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const char* platformJson = std::getenv("FACTORY_PLATFORM_JSON");
    if (!platformJson) platformJson = "/oem/usr/conf/platform.json";

    const char* testsJson = std::getenv("FACTORY_TESTS_JSON");
    if (!testsJson) testsJson = "/oem/usr/conf/tests.json";

    auto& cfg = PlatformConfig::instance();
    if (!cfg.loadFromFile(platformJson)) {
        fprintf(stderr, "[main] failed to load %s\n", platformJson);
        return -1;
    }

    auto platform = ft::createPlatform(cfg.platformName().c_str());
    if (!platform || !platform->init(cfg.raw())) {
        fprintf(stderr, "[main] platform init failed\n");
        return -1;
    }

    ft::DriverRegistry reg;
    platform->registerDrivers(reg);

    auto display = reg.create<IDisplayDriver>();
    if (display) display->init();

    auto& statusLed = StatusLedController::instance();
    if (statusLed.configure(cfg.raw())) {
        statusLed.start(StatusLedMode::Normal);
    }

    ft::TestEngine engine(reg, cfg);
    if (!engine.loadTestConfig(testsJson)) {
        fprintf(stderr, "[main] failed to load %s\n", testsJson);
        statusLed.stop();
        return -1;
    }

    if (std::getenv("FACTORY_SELF_TEST")) {
        fprintf(stdout, "[SelfTest] triggering battery (sync)...\n");
        engine.onMqttMessage("15R", "");
        fprintf(stdout, "[SelfTest] triggering camera (async)...\n");
        engine.onMqttMessage("18R", "");
        // Give async worker time to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        fprintf(stdout, "[SelfTest] triggering sys (async)...\n");
        engine.onMqttMessage("26R", "");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        engine.stop();
        statusLed.stop();
        fprintf(stdout, "[SelfTest] done\n");
        return 0;
    }

    std::thread engineThread([&engine]() {
        engine.run();
    });

    while (!gQuit) {
        uint32_t sleepMs = 10;
        if (display && display->isInitialized()) {
            sleepMs = display->taskHandler();
            sleepMs = std::clamp<uint32_t>(sleepMs, 1, 10);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    engine.stop();
    engineThread.join();
    statusLed.stop();
    if (display) display->deinit();
    return 0;
}
