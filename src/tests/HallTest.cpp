// Hall test: 21R / 27R
// 21R: vertical hall sensor test (upper/lower limit detection)
// 27R: horizontal hall sensor test (360° voltage calibration)
//
// 21R error_code: bit0=upper limit not detected, bit1=lower limit not detected
// 27R error_code: bit0=hall not initialized, bit2=max voltage<2.0V, bit3=min voltage<1.6V

#include "tests/HallTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "hal/MotorController.h"
#include "hal/HallSwitch.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace ft {

static float normalize_angle_360(float angle) {
    while (angle >= 360.0f) angle -= 360.0f;
    while (angle < 0.0f)    angle += 360.0f;
    return angle;
}

static float angle_delta_180(float current, float last) {
    float d = current - last;
    if (d < -180.0f) d += 360.0f;
    else if (d > 180.0f) d -= 360.0f;
    return d;
}

// Horizontal hall test: rotate 360°+ while sampling voltage
static int hall_test_360(MotorDirection dir, enum motor_speed_gear speed) {
    constexpr float HALL_VOLTAGE_TOLERANCE = 0.005f;
    constexpr int HALL_CALIBRATE_TIMEOUT_MS = 15000;
    constexpr int HALL_CALIBRATE_MAX_SAMPLES = 500;
    constexpr int SAMPLE_INTERVAL_MS = 30;

    float max_voltage = 0.0f, min_voltage = 3.3f;
    int sample_count = 0;

    float start_angle = normalize_angle_360(MotorController::instance().getPosition(dir));
    float last_raw_angle = start_angle;
    int stable_count = 0;

    float total_rotation = 365.0f;
    MotorController::instance().start(dir, total_rotation, speed, FORWARD);

    int elapsed_ms = 0;
    while (elapsed_ms < HALL_CALIBRATE_TIMEOUT_MS) {
        float current_angle = normalize_angle_360(MotorController::instance().getPosition(dir));
        float delta = angle_delta_180(current_angle, last_raw_angle);
        last_raw_angle = current_angle;

        if (elapsed_ms >= 4000) {
            if (fabsf(delta) < 0.1f) {
                stable_count++;
                if (stable_count >= 10) {
                    std::fprintf(stderr, "[hall] motor stopped at %.1f°, elapsed=%dms\n",
                                 current_angle, elapsed_ms);
                    break;
                }
            } else {
                stable_count = 0;
            }
        }

        float voltage = hall_voltage_get();
        if (sample_count < HALL_CALIBRATE_MAX_SAMPLES) {
            sample_count++;
            if (voltage > max_voltage) max_voltage = voltage;
            if (voltage < min_voltage) min_voltage = voltage;
        } else {
            break;
        }

        usleep(SAMPLE_INTERVAL_MS * 1000);
        elapsed_ms += SAMPLE_INTERVAL_MS;
    }

    MotorController::instance().stop(dir);

    std::fprintf(stderr, "[hall] samples=%d, max=%.3fV, min=%.3fV\n",
                 sample_count, max_voltage, min_voltage);

    if (max_voltage < 2.0f - HALL_VOLTAGE_TOLERANCE) return 2;
    if (min_voltage < 1.60f - HALL_VOLTAGE_TOLERANCE) return 3;
    return 0;
}

void register_hall_tests(TestEngine& engine) {

    // 21R: Vertical hall (upper/lower limit detection)
    engine.registerTest("21R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;
        MotorDirection dir = MOTOR_VERTICAL;

        // Move down until limit (detect by position stall, timeout 10s)
        float start_pos = MotorController::instance().getPosition(dir);
        MotorController::instance().start(dir, 360.0f, MOTOR_SPEED_1_94_3, FORWARD);

        int timeout = 100;
        float last_pos = start_pos;
        bool lower_detected = false;
        while (timeout-- > 0) {
            usleep(100000);
            float pos = MotorController::instance().getPosition(dir);
            float d = pos - last_pos;
            if (d < 0) d = -d;
            if (d < 0.05f) {
                lower_detected = true;
                break;
            }
            last_pos = pos;
        }
        MotorController::instance().stop(dir);

        if (lower_detected) {
            std::fprintf(stderr, "[21R] bottom limit detected\n");
        } else {
            err |= 0x0002;
            std::fprintf(stderr, "[21R] bottom limit NOT detected\n");
        }

        // Move up until limit
        start_pos = MotorController::instance().getPosition(dir);
        MotorController::instance().start(dir, -360.0f, MOTOR_SPEED_1_94_3, BACK);

        timeout = 100;
        last_pos = start_pos;
        bool upper_detected = false;
        while (timeout-- > 0) {
            usleep(100000);
            float pos = MotorController::instance().getPosition(dir);
            float d = pos - last_pos;
            if (d < 0) d = -d;
            if (d < 0.05f) {
                upper_detected = true;
                break;
            }
            last_pos = pos;
        }
        MotorController::instance().stop(dir);

        if (upper_detected) {
            std::fprintf(stderr, "[21R] top limit detected\n");
        } else {
            err |= 0x0001;
            std::fprintf(stderr, "[21R] top limit NOT detected\n");
        }

        // Return to center
        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);

        std::fprintf(stderr, "[21R] done, err=0x%08X\n", err);
        return err;
    }, /*async=*/true);

    // 27R: Horizontal hall (voltage sampling calibration)
    engine.registerTest("27R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;

        if (!hall_switch_is_initialized()) {
            err |= 0x0001;
            std::fprintf(stderr, "[27R] hall position manager not initialized\n");
            return err;
        }

        int ret = hall_test_360(MOTOR_HORIZONTAL, MOTOR_SPEED_1_94_3);
        if (ret != 0) {
            err |= (1u << ret);
            std::fprintf(stderr, "[27R] hall test failed, ret=%d\n", ret);
        }

        std::fprintf(stderr, "[27R] done, err=0x%08X\n", err);
        return err;
    }, /*async=*/true);
}

} // namespace ft
