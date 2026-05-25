// main.cpp - Factory test main entry
// Architecture: TestEngine (MQTT + dispatch + async) + HAL + test modules

#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include "hal/MotorController.h"
#include "hal/HallSwitch.h"
#include "hal/GpioController.h"
#include "hal/FactoryDisplay.h"
#include "hal/RecorderController.h"
#include "hal/RecorderImpl.h"

#include "tests/BatteryTest.h"
#include "tests/CameraTest.h"
#include "tests/WifiTest.h"
#include "tests/RtcTest.h"
#include "tests/KeyTest.h"
#include "tests/MicTest.h"
#include "tests/SocTest.h"
#include "tests/TfCardTest.h"
#include "tests/MotorTest.h"
#include "tests/HallTest.h"
#include "tests/AgingTest.h"
#include "tests/SysTest.h"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>

static void on_signal(int) { ft::globals().running = false; }

int main() {
    auto& g = ft::globals();

    // ---- Print version info ----
    std::system("echo version: $(cat /oem/usr/conf/version.txt 2>/dev/null || echo '<undefined>')");
    std::system("echo pcba_sn: $(cat /device_data/pcba.txt 2>/dev/null || echo '<undefined>')");

    // ---- TestEngine (MQTT + dispatch + async + LED in one) ----
    ft::TestEngine engine;

    if (engine.startMqtt("prodTest") != 0) {
        std::fprintf(stderr, "[main] MQTT start failed\n");
        return -1;
    }

    // ---- LED thread ----
    engine.startLedThread();

    // ---- Motor + Hall init ----
    ft::MotorController::instance().init();
    if (hall_switch_init() != 0) {
        std::fprintf(stderr, "[main] hall_switch_init failed, hall test will not work\n");
    }

    // ---- LED GPIO init (for mic test buzzer) ----
    // white
    ft::GpioController::exportGpio(170);
    ft::GpioController::setDirection(170, "out");
    ft::GpioController::write(170, 1);

    // red
    ft::GpioController::exportGpio(169);
    ft::GpioController::setDirection(169, "out");
    ft::GpioController::write(169, 1);

    // ---- LVGL Factory display ----
    ft::FactoryDisplay::instance().init();

    // Read initial battery value for display
    {
        std::string uevent = ft::read_sysfs("/sys/class/power_supply/cw221X-bat/uevent");
        if (!uevent.empty()) {
            size_t p = uevent.find("POWER_SUPPLY_CAPACITY=");
            if (p != std::string::npos) {
                int cap = std::atoi(uevent.c_str() + p + 23);
                ft::FactoryDisplay::instance().setBatteryPercent(cap);
            }
        }
    }

    // ---- V4L2 Recorder init ----
    {
        std::ifstream cfg_file("/oem/usr/conf/recorder.json");
        if (cfg_file.is_open()) {
            try {
                nlohmann::json j;
                cfg_file >> j;
                ft::RecorderConfig rcfg;
                rcfg.config_source = "/oem/usr/conf/recorder.json";
                if (j.contains("max_duration_sec"))
                    rcfg.max_duration_sec = j["max_duration_sec"];
                if (j.contains("cameras") && j["cameras"].is_array()) {
                    for (auto& c : j["cameras"]) {
                        ft::CameraConfig cc;
                        if (c.contains("device")) cc.device = c["device"];
                        if (c.contains("width"))  cc.width  = c["width"];
                        if (c.contains("height")) cc.height = c["height"];
                        if (c.contains("format")) cc.format = c["format"];
                        if (c.contains("fps"))    cc.fps    = c["fps"];
                        if (c.contains("output")) cc.output = c["output"];
                        if (c.contains("buffer_count")) cc.buffer_count = c["buffer_count"];
                        rcfg.cameras.push_back(cc);
                    }
                }
                if (j.contains("audio") && j["audio"].is_object()) {
                    auto& a = j["audio"];
                    if (a.contains("enabled"))     rcfg.audio.enabled     = a["enabled"];
                    if (a.contains("device"))      rcfg.audio.device      = a["device"];
                    if (a.contains("sample_rate")) rcfg.audio.sample_rate = a["sample_rate"];
                    if (a.contains("channels"))    rcfg.audio.channels    = a["channels"];
                    if (a.contains("gain_db"))     rcfg.audio.gain_db     = a["gain_db"];
                }
                if (rcfg.valid()) {
                    ft::RecorderController::instance().setRecorder(
                        ft::createV4l2Recorder(rcfg));
                    std::fprintf(stderr, "[main] V4L2 recorder configured: %zu camera(s)\n",
                                 rcfg.cameras.size());
                } else {
                    std::fprintf(stderr, "[main] recorder config has no cameras, using null\n");
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[main] recorder config parse error: %s, using null\n", e.what());
            }
        } else {
            std::fprintf(stderr, "[main] no recorder config found, using null (set /oem/usr/conf/recorder.json to enable)\n");
        }
    }

    // ---- Register all test modules ----
    engine.start();

    ft::register_battery_tests(engine);   // 15R 24R
    ft::register_camera_tests(engine);    // 18R 22R 28R
    ft::register_wifi_tests(engine);      // 16R 37R
    ft::register_rtc_tests(engine);       // 10R
    ft::register_key_tests(engine);       // 17R 20R
    ft::register_mic_tests(engine);       // 11R 23R
    ft::register_soc_tests(engine);       // 12R
    ft::register_tf_tests(engine);        // 14R BFA
    ft::register_motor_tests(engine);     // 191R 192R 291R-296R
    ft::register_hall_tests(engine);      // 21R 27R
    ft::register_aging_tests(engine);     // 30R 32R 34R 36R
    ft::register_sys_tests(engine);       // 26R 31R 50R

    // ---- Version info request ----
    {
        std::string sn = ft::read_sysfs("/device_data/pcba.txt");
        if (sn.size() >= 14) {
            engine.publish("AZR", std::string(1, '\x01'), 2);
        }
    }

    std::fprintf(stderr, "[main] factory_test running (prodTest)\n");

    // ---- Main loop with LVGL ----
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    while (g.running) {
        uint32_t ms = ft::FactoryDisplay::instance().taskHandler();
        uint32_t sleep_ms = (ms > 10) ? 10 : ms;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms ? sleep_ms : 5));
    }

    // ---- Clean shutdown ----
    engine.stop();
    engine.stopMqtt();
    ft::MotorController::instance().deinit();
    hall_switch_deinit();

    std::fprintf(stderr, "[main] factory_test exited cleanly\n");
    return 0;
}
