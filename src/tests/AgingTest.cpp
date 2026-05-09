// Aging test: 30R (full aging), 32R (motor aging), 34R (stop), 36R (fail)
// Monitors WiFi/CAM, controls motors (recording/tracking decoupled from multi_media)
// Uses JSON for aging result output

#include "tests/AgingTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include "hal/MotorController.h"
#include "hal/RecorderController.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>

namespace ft {

using json = nlohmann::json;

static constexpr int CHECK_INTERVAL = 10;

static int get_aging_time_seconds() {
    std::string content = read_sysfs("/userdata/aging_time.conf");
    if (content.empty()) return 4 * 3600;
    if (content == "inf") return -1;
    int hours = std::atoi(content.c_str());
    if (hours <= 0) return 4 * 3600;
    return hours * 3600;
}

static bool camera_is_ok() {
    auto r = shell_exec("dmesg | tail -n 300");
    return r.output.find("MIPI_CSI2 ERR") == std::string::npos;
}

static bool wifi_scan_failed() {
    auto r = shell_exec("wpa_cli scan_res 2>&1");
    return r.output.find("Failed") != std::string::npos;
}

static void save_result(const std::string& result, const std::string& reason) {
    json j;
    j["sn"] = read_sysfs("/device_data/pcba.txt");
    j["test_item"] = "aging";
    j["result"] = result;
    j["fail_reason"] = reason;
    j["timestamp"] = (int64_t)std::time(nullptr);

    std::string s = j.dump(2);
    FILE* f = std::fopen("/userdata/aging_result.json", "w");
    if (f) {
        std::fwrite(s.data(), 1, s.size(), f);
        std::fclose(f);
    }
}

// ---- Motor aging threads ----
static void aging_vertical_loop() {
    auto& g = globals();
    MotorDirection dir = MOTOR_VERTICAL;
    while (g.aging_motor_test_start) {
        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!g.aging_motor_test_start) break;
        MotorController::instance().start(dir, -150.0f, MOTOR_SPEED_1_94_3, BACK);
        std::this_thread::sleep_for(std::chrono::seconds(15));
        if (!g.aging_motor_test_start) break;
        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    MotorController::instance().stop(dir);
}

static void aging_horizontal_loop() {
    auto& g = globals();
    MotorDirection dir = MOTOR_HORIZONTAL;
    while (g.aging_motor_test_start) {
        MotorController::instance().start(dir, 90.0f, MOTOR_SPEED_1_94_3, FORWARD);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!g.aging_motor_test_start) break;
        MotorController::instance().start(dir, -180.0f, MOTOR_SPEED_1_94_3, BACK);
        std::this_thread::sleep_for(std::chrono::seconds(15));
        if (!g.aging_motor_test_start) break;
        MotorController::instance().start(dir, 90.0f, MOTOR_SPEED_1_94_3, FORWARD);
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    MotorController::instance().stop(dir);
}

static std::thread g_aging_main_thread;
static std::thread g_aging_v_motor_thread;
static std::thread g_aging_h_motor_thread;

static void stop_all_aging() {
    auto& g = globals();
    g.aging_test_start = false;
    g.aging_motor_test_start = false;

    if (g_aging_main_thread.joinable()) g_aging_main_thread.join();
    if (g_aging_v_motor_thread.joinable()) g_aging_v_motor_thread.join();
    if (g_aging_h_motor_thread.joinable()) g_aging_h_motor_thread.join();

}

// ---- Aging main loop ----
static void aging_main_loop() {
    auto& g = globals();
    unsigned int duration = 0;
    int aging_time = get_aging_time_seconds();

    std::fprintf(stderr, "[aging] main loop start, target=%ds\n", aging_time);
    std::system("echo 2 > /userdata/aging_completed.txt");

    RecorderController::instance().start();

    while (g.aging_test_start) {
        if (aging_time > 0 && (int)duration >= aging_time) {
            set_led_mode(MODE_TEST_DONE);
            std::system("echo 1 > /userdata/aging_completed.txt");
            save_result("pass", "");
            g.aging_test_start = false;
            std::fprintf(stderr, "[aging] completed, duration=%ds\n", duration);
            break;
        }

        std::system("wpa_cli scan > /dev/null");
        if (wifi_scan_failed()) {
            std::system("echo 32 > /userdata/aging_completed.txt");
            save_result("fail", "WIFI");
            set_led_mode(MODE_TEST_FAIL);
            g.aging_test_start = false;
            g.aging_motor_test_start = false;
            std::fprintf(stderr, "[aging] WIFI FAIL\n");
        }

        if (!camera_is_ok()) {
            std::system("echo 34 > /userdata/aging_completed.txt");
            save_result("fail", "CAM");
            set_led_mode(MODE_TEST_FAIL);
            g.aging_test_start = false;
            g.aging_motor_test_start = false;
            std::fprintf(stderr, "[aging] CAM FAIL\n");
        }

        std::this_thread::sleep_for(std::chrono::seconds(CHECK_INTERVAL));
        duration += CHECK_INTERVAL;
    }

    RecorderController::instance().stop();
    std::fprintf(stderr, "[aging] main loop exit\n");
}

void register_aging_tests(TestEngine& engine) {

    // 30R: Full aging test
    engine.registerRaw("30R", [](const uint8_t*, size_t) {
        auto& g = globals();

        set_led_mode(MODE_TEST);

        g.aging_test_start = true;
        g.aging_motor_test_start = true;

        if (g_aging_main_thread.joinable()) g_aging_main_thread.join();
        g_aging_main_thread = std::thread(aging_main_loop);

        if (g_aging_v_motor_thread.joinable()) g_aging_v_motor_thread.join();
        g_aging_v_motor_thread = std::thread(aging_vertical_loop);

        if (g_aging_h_motor_thread.joinable()) g_aging_h_motor_thread.join();
        g_aging_h_motor_thread = std::thread(aging_horizontal_loop);

        std::fprintf(stderr, "[30R] full aging started\n");
    });

    // 32R: Motor-only aging
    engine.registerRaw("32R", [](const uint8_t*, size_t) {
        auto& g = globals();
        g.aging_motor_test_start = true;

        if (g_aging_v_motor_thread.joinable()) g_aging_v_motor_thread.join();
        g_aging_v_motor_thread = std::thread(aging_vertical_loop);

        if (g_aging_h_motor_thread.joinable()) g_aging_h_motor_thread.join();
        g_aging_h_motor_thread = std::thread(aging_horizontal_loop);

        std::fprintf(stderr, "[32R] motor aging started\n");
    });

    // 34R: Aging stopped (normal completion)
    engine.registerRaw("34R", [](const uint8_t*, size_t) {
        set_led_mode(MODE_NORMAL);
        std::system("echo 1 > /userdata/aging_completed.txt");
        stop_all_aging();
        std::fprintf(stderr, "[34R] aging stopped (normal)\n");
    });

    // 36R: Aging failed (marked by host)
    engine.registerRaw("36R", [](const uint8_t*, size_t) {
        set_led_mode(MODE_TEST_FAIL);
        std::system("echo 30 > /userdata/aging_completed.txt");
        stop_all_aging();
        std::fprintf(stderr, "[36R] aging marked as FAILED\n");
    });
}

} // namespace ft
