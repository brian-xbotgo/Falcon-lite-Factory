// Hall test: 21R / 27R
// 21R: vertical hall sensor test (upper/lower limit detection)
// 27R: horizontal hall sensor test (360° magnetic field strength calibration)
//
// 21R error_code: bit0=upper limit not detected, bit1=lower limit not detected
// 27R error_code: bit0=hall not initialized, bit2=max field too weak, bit3=field variation too small

#include "tests/HallTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "hal/MotorController.h"
#include "hal/HallSwitch.h"
#include "hal/GpioController.h"
#include "iniparser.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <atomic>
#include <poll.h>
#include <fcntl.h>

namespace ft {

// 垂直电机限位 GPIO
constexpr int GPIO_HALL_UPPER_LIMIT = 181;
constexpr int GPIO_HALL_LOWER_LIMIT = 182;

// 21R 错误码（与老厂测一致）
constexpr uint32_t ERR_V_TOP_HALL_FAIL = 0x0001; // bit0: 上限位未检测到
constexpr uint32_t ERR_V_BOT_HALL_FAIL = 0x0002; // bit1: 下限位未检测到

// 27R hall_test_360 返回码
constexpr int HALL_TEST_OK                = 0; // 测试通过
constexpr int HALL_ERR_MAX_FIELD_WEAK     = 2; // bit2: 峰值磁场过弱
constexpr int HALL_ERR_VARIATION_SMALL    = 3; // bit3: 磁场变化量不足

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

// Horizontal hall test: rotate 360°+ while sampling magnetic field strength
static int hall_test_360(MotorDirection dir, enum motor_speed_gear speed) {
    constexpr float HALL_INVALID_VALUE      = 9999.0f;  // 读取失败标记值
    constexpr int   HALL_CALIBRATE_TIMEOUT_MS = 15000;
    constexpr int   HALL_CALIBRATE_MAX_SAMPLES = 500;
    constexpr int   SAMPLE_INTERVAL_MS = 30;

    // 从 ini 读取阈值
    dictionary* ini = iniparser_load("/userdata/hall_threshold.ini");
    float max_field_min = (float)iniparser_getdouble(ini, "hall_threshold:max_field_min", 20.0);
    float variation_min = (float)iniparser_getdouble(ini, "hall_threshold:variation_min", 20.0);
    iniparser_freedict(ini);
    std::fprintf(stderr, "[hall] thresholds: max_field_min=%.2f, variation_min=%.2f\n",
                 max_field_min, variation_min);

    float max_field = 0.0f;
    float min_field = 9999.0f;
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

        float field = hall_value_get();
        if (field >= HALL_INVALID_VALUE) {
            // 读取失败，跳过本次采样
            usleep(SAMPLE_INTERVAL_MS * 1000);
            elapsed_ms += SAMPLE_INTERVAL_MS;
            continue;
        }

        field = fabsf(field); // 磁场强度取绝对值

        if (sample_count < HALL_CALIBRATE_MAX_SAMPLES) {
            sample_count++;
            if (field > max_field) max_field = field;
            if (field < min_field) min_field = field;
        } else {
            break;
        }

        usleep(SAMPLE_INTERVAL_MS * 1000);
        elapsed_ms += SAMPLE_INTERVAL_MS;
    }

    MotorController::instance().stop(dir);

    // 无有效采样
    if (sample_count == 0) {
        std::fprintf(stderr, "[hall] no valid samples\n");
        return HALL_ERR_MAX_FIELD_WEAK;
    }

    float variation = max_field - min_field;
    std::fprintf(stderr, "[hall] samples=%d, max=%.2fmT, min=%.2fmT, variation=%.2fmT\n",
                 sample_count, max_field, min_field, variation);

    if (max_field < max_field_min) return HALL_ERR_MAX_FIELD_WEAK;
    if (variation < variation_min) return HALL_ERR_VARIATION_SMALL;
    return HALL_TEST_OK;
}

// ------------------------------------------------------------------
// GPIO limit detection helper (for 21R vertical motor)
// ------------------------------------------------------------------
struct GpioLimit {
    int gpio = -1;
    int fd = -1;
    std::atomic<bool> reached{false};
    std::atomic<bool> stop{false};
    std::thread thread;

    GpioLimit() = default;
    GpioLimit(const GpioLimit&) = delete;
    GpioLimit& operator=(const GpioLimit&) = delete;

    ~GpioLimit() {
        stop.store(true);
        if (thread.joinable()) thread.join();
        if (fd >= 0) { ::close(fd); fd = -1; }
        if (gpio >= 0) GpioController::unexport(gpio);
    }
};

static void gpioPollThread(GpioLimit* limit) {
    struct pollfd pfd;
    pfd.fd = limit->fd;
    pfd.events = POLLPRI | POLLERR;
    char buf[8];

    while (!limit->stop.load()) {
        int ret = poll(&pfd, 1, 100);
        if (ret > 0 && (pfd.revents & POLLPRI)) {
            lseek(limit->fd, 0, SEEK_SET);
            if (read(limit->fd, buf, sizeof(buf)) > 0) {
                limit->reached.store(true);
            }
        }
    }
}

static bool setupGpioLimit(GpioLimit& limit, int gpio) {
    limit.gpio = gpio;
    if (!GpioController::exportGpio(gpio)) {
        std::fprintf(stderr, "[21R] export gpio %d failed\n", gpio);
        return false;
    }
    if (!GpioController::setDirection(gpio, "in")) {
        std::fprintf(stderr, "[21R] set gpio %d direction failed\n", gpio);
        return false;
    }
    if (!GpioController::setEdge(gpio, "both")) {
        std::fprintf(stderr, "[21R] set gpio %d edge failed\n", gpio);
        return false;
    }

    std::string path = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/value";
    limit.fd = open(path.c_str(), O_RDONLY);
    if (limit.fd < 0) {
        std::fprintf(stderr, "[21R] open gpio %d value failed\n", gpio);
        return false;
    }

    // clear pending interrupt
    char buf[8];
    lseek(limit.fd, 0, SEEK_SET);
    read(limit.fd, buf, sizeof(buf));

    limit.reached.store(false);
    limit.stop.store(false);
    limit.thread = std::thread(gpioPollThread, &limit);
    return true;
}

void register_hall_tests(TestEngine& engine) {

    // 21R: Vertical hall (upper/lower limit detection via GPIO)
    engine.registerTest("21R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;
        MotorDirection dir = MOTOR_VERTICAL;

        // 从 ini 读取 GPIO 号
        dictionary* ini = iniparser_load("/userdata/hall_threshold.ini");
        int upper_gpio = iniparser_getint(ini, "hall_threshold:upper_limit_gpio", 181);
        int lower_gpio = iniparser_getint(ini, "hall_threshold:lower_limit_gpio", 182);
        iniparser_freedict(ini);
        std::fprintf(stderr, "[21R] GPIO config: upper=%d, lower=%d\n", upper_gpio, lower_gpio);

        GpioLimit upperLimit;
        GpioLimit lowerLimit;
        if (!setupGpioLimit(upperLimit, upper_gpio)) {
            err |= ERR_V_TOP_HALL_FAIL;
            return err;
        }
        if (!setupGpioLimit(lowerLimit, lower_gpio)) {
            err |= ERR_V_BOT_HALL_FAIL;
            return err;
        }

        // --- Move down until lower limit ---
        lowerLimit.reached.store(false);
        MotorController::instance().start(dir, 360.0f, MOTOR_SPEED_1_94_3, FORWARD);

        int timeout = 100; // 100 * 100ms = 10s
        while (timeout-- > 0 && !lowerLimit.reached.load()) {
            usleep(100000);
        }
        MotorController::instance().stop(dir);

        if (lowerLimit.reached.load()) {
            std::fprintf(stderr, "[21R] bottom limit detected\n");
        } else {
            err |= ERR_V_BOT_HALL_FAIL;
            std::fprintf(stderr, "[21R] bottom limit NOT detected\n");
        }

        // --- Move up until upper limit ---
        upperLimit.reached.store(false);
        MotorController::instance().start(dir, -360.0f, MOTOR_SPEED_1_94_3, BACK);

        timeout = 100;
        while (timeout-- > 0 && !upperLimit.reached.load()) {
            usleep(100000);
        }
        MotorController::instance().stop(dir);

        if (upperLimit.reached.load()) {
            std::fprintf(stderr, "[21R] top limit detected\n");
        } else {
            err |= ERR_V_TOP_HALL_FAIL;
            std::fprintf(stderr, "[21R] top limit NOT detected\n");
        }

        // Return to center
        MotorController::instance().start(dir, 75.0f, MOTOR_SPEED_1_94_3, FORWARD);

        std::fprintf(stderr, "[21R] done, err=0x%08X\n", err);
        return err;
    }, /*async=*/true);

    // 27R: Horizontal hall (magnetic field strength sampling calibration)
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
