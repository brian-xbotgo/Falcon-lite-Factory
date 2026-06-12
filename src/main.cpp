#include "config/PlatformConfig.h"
#include "core/DriverRegistry.h"
#include "core/TestEngine.h"
#include "control/StatusLedController.h"
#include "platforms/common/interface/IPlatform.h"
#include "platforms/common/interface/IDisplayDriver.h"
#ifdef HAVE_MOSQUITTO
#include "ble/BleAdvertiser.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace ft;

namespace {

std::atomic<bool> gQuit{false};

void handleSignal(int)
{
    gQuit = true;
}

void mirrorLogsForFactoryTool()
{
    mkdir("/userdata", 0755);
    mkdir("/userdata/logs", 0755);
    const int logFd = open("/userdata/logs/prod_test_new.log",
                           O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0) return;

    const int consoleFd = dup(STDOUT_FILENO);
    int pipeFd[2];
    if (pipe(pipeFd) != 0) {
        close(logFd);
        if (consoleFd >= 0) close(consoleFd);
        return;
    }

    dup2(pipeFd[1], STDOUT_FILENO);
    dup2(pipeFd[1], STDERR_FILENO);
    close(pipeFd[1]);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::thread([readFd = pipeFd[0], logFd, consoleFd]() {
        char buffer[1024];
        while (true) {
            const ssize_t n = read(readFd, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (consoleFd >= 0) write(consoleFd, buffer, static_cast<size_t>(n));
            write(logFd, buffer, static_cast<size_t>(n));
        }
        if (consoleFd >= 0) close(consoleFd);
        close(logFd);
        close(readFd);
    }).detach();
}

} // namespace

int main() {
    mirrorLogsForFactoryTool();

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

#ifdef HAVE_MOSQUITTO
    ft::BleAdvertiser bleAdvertiser;
    std::thread bleThread;
    const char* bleEnabled = std::getenv("FACTORY_ENABLE_BLE");
    if (!bleEnabled || std::string(bleEnabled) != "0") {
        bleThread = std::thread([&bleAdvertiser]() {
            const int rc = bleAdvertiser.run();
            if (rc != 0) {
                std::fprintf(stderr, "[main] BLE service exited rc=%d\n", rc);
            }
        });
    }
#endif

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
#ifdef HAVE_MOSQUITTO
    bleAdvertiser.shutdown();
    if (bleThread.joinable()) {
        bleThread.join();
    }
#endif
    statusLed.stop();
    if (display) display->deinit();
    return 0;
}
