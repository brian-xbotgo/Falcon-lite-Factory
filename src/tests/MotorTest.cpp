// Motor test: 191R/192R (board), 291R-296R (system)
// 191R: vertical motor board test (CH34)
// 192R: horizontal motor board test (CH12)
// 291R/292R: start motor loop threads
// 293R/294R: stop motor threads
// 295R/296R: motor + audio recording

#include "tests/MotorTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include "hal/MotorController.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace ft {

// Wait for motor to stop moving (position stable)
static void motor_check_stopped(MotorDirection dir, int timeout_sec, int interval_sec) {
    float last_pos = MotorController::instance().getPosition(dir);
    int stable = 0;
    int max_checks = (timeout_sec * 1000) / (interval_sec * 1000);

    for (int i = 0; i < max_checks; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_sec * 1000));
        float pos = MotorController::instance().getPosition(dir);
        float delta = pos - last_pos;
        if (delta < 0) delta = -delta;
        if (delta < 0.1f) {
            stable++;
            if (stable >= 3) return;
        } else {
            stable = 0;
        }
        last_pos = pos;
    }
}

// ---- 291R: Vertical motor continuous loop ----
static void vertical_motor_loop() {
    auto& g = globals();
    MotorDirection dir = MOTOR_VERTICAL;

    std::fprintf(stderr, "[291R] vertical motor loop start\n");
    while (g.aging_motor_test_start) {
        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);
        motor_check_stopped(dir, 10, 1);
        if (!g.aging_motor_test_start) break;

        MotorController::instance().start(dir, -150.0f, MOTOR_SPEED_1_94_3, BACK);
        motor_check_stopped(dir, 10, 1);
        if (!g.aging_motor_test_start) break;

        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);
        motor_check_stopped(dir, 10, 1);
    }
    MotorController::instance().stop(dir);
    std::fprintf(stderr, "[291R] vertical motor loop exit\n");
}

// ---- 292R: Horizontal motor continuous loop ----
static void horizontal_motor_loop() {
    auto& g = globals();
    MotorDirection dir = MOTOR_HORIZONTAL;

    std::fprintf(stderr, "[292R] horizontal motor loop start\n");
    while (g.aging_motor_test_start) {
        MotorController::instance().start(dir, 90.0f, MOTOR_SPEED_1_94_3, FORWARD);
        motor_check_stopped(dir, 10, 1);
        if (!g.aging_motor_test_start) break;

        MotorController::instance().start(dir, -180.0f, MOTOR_SPEED_1_94_3, BACK);
        motor_check_stopped(dir, 10, 1);
        if (!g.aging_motor_test_start) break;

        MotorController::instance().start(dir, 90.0f, MOTOR_SPEED_1_94_3, FORWARD);
        motor_check_stopped(dir, 10, 1);
    }
    MotorController::instance().stop(dir);
    std::fprintf(stderr, "[292R] horizontal motor loop exit\n");
}

// ---- 295R/296R: Motor + mic test ----
static uint32_t motor_mic_test(MotorDirection dir, const char* wav_path, float angle) {
    std::system("mkdir -p /userdata/prod");
    std::string rm_cmd = std::string("rm -f ") + wav_path;
    std::system(rm_cmd.c_str());

    MotorController::instance().start(dir, angle, MOTOR_SPEED_1_94_3, FORWARD);

    std::string rec_cmd = std::string("arecord -D hw:1,0 -f S32_LE -r 48000 -c 2 -t wav -d 10 ") + wav_path;
    shell_exec(rec_cmd);

    motor_check_stopped(dir, 10, 1);
    MotorController::instance().stop(dir);

    return 0;
}

static std::thread g_v_motor_thread;
static std::thread g_h_motor_thread;

void register_motor_tests(TestEngine& engine) {

    // 191R: Vertical motor board test (CH34, no response)
    engine.registerRaw("191R", [](const uint8_t*, size_t) {
        std::fprintf(stderr, "[191R] vertical motor board test\n");
        MotorController::instance().startBoardTest(MOTOR_VERTICAL, 1024, SUBDIVIDE128, MOTOR_SPEED_1_94_3, FORWARD);
    });

    // 192R: Horizontal motor board test (CH12, no response)
    engine.registerRaw("192R", [](const uint8_t*, size_t) {
        std::fprintf(stderr, "[192R] horizontal motor board test\n");
        MotorController::instance().startBoardTest(MOTOR_HORIZONTAL, 1024, SUBDIVIDE128, MOTOR_SPEED_1_94_3, FORWARD);
    });

    // 291R: Start vertical motor continuous test
    engine.registerRaw("291R", [](const uint8_t*, size_t) {
        auto& g = globals();
        g.aging_motor_test_start = true;
        if (g_v_motor_thread.joinable()) g_v_motor_thread.join();
        g_v_motor_thread = std::thread(vertical_motor_loop);
        std::fprintf(stderr, "[291R] vertical motor test started\n");
    });

    // 292R: Start horizontal motor continuous test
    engine.registerRaw("292R", [](const uint8_t*, size_t) {
        auto& g = globals();
        g.aging_motor_test_start = true;
        if (g_h_motor_thread.joinable()) g_h_motor_thread.join();
        g_h_motor_thread = std::thread(horizontal_motor_loop);
        std::fprintf(stderr, "[292R] horizontal motor test started\n");
    });

    // 293R: Stop vertical motor
    engine.registerRaw("293R", [](const uint8_t*, size_t) {
        globals().aging_motor_test_start = false;
        if (g_v_motor_thread.joinable()) g_v_motor_thread.join();
        std::fprintf(stderr, "[293R] vertical motor stopped\n");
    });

    // 294R: Stop horizontal motor
    engine.registerRaw("294R", [](const uint8_t*, size_t) {
        globals().aging_motor_test_start = false;
        if (g_h_motor_thread.joinable()) g_h_motor_thread.join();
        std::fprintf(stderr, "[294R] horizontal motor stopped\n");
    });

    // 295R → 295A: Vertical motor + audio
    engine.registerTest("295R", [](const ProtoHeader&) -> uint32_t {
        return motor_mic_test(MOTOR_VERTICAL, "/userdata/prod/v_motor.wav", 75.0f);
    }, /*async=*/true);

    // 296R → 296A: Horizontal motor + audio
    engine.registerTest("296R", [](const ProtoHeader&) -> uint32_t {
        return motor_mic_test(MOTOR_HORIZONTAL, "/userdata/prod/h_motor.wav", 90.0f);
    }, /*async=*/true);
}

} // namespace ft
