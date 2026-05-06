#include "hal/MotorController.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace ft {

MotorController& MotorController::instance() {
    static MotorController mc;
    return mc;
}

enum channel_select MotorController::toChannel(MotorDirection dir) {
    return (dir == MOTOR_HORIZONTAL) ? CH12 : CH34;
}

enum direct_select MotorController::angleToDirect(float angleDeg) {
    return (angleDeg >= 0) ? FORWARD : BACK;
}

bool MotorController::init() {
    if (initialized_) return true;
    if (motor_hal_init("/dev/tmi8152") != 0) return false;
    initialized_ = true;
    return true;
}

void MotorController::deinit() {
    motor_hal_deinit();
    initialized_ = false;
}

bool MotorController::start(MotorDirection dir, float angleDeg, enum motor_speed_gear speed, enum direct_select direct) {
    if (!initialized_) return false;
    return motor_hal_start(toChannel(dir), angleDeg, speed, direct) == 0;
}

bool MotorController::startBoardTest(MotorDirection dir, unsigned int cycles, enum subdivide_select subdivide, enum motor_speed_gear speed, enum direct_select direct) {
    if (!initialized_) return false;

    int fd = open("/dev/tmi8152", O_RDWR);
    if (fd < 0) { std::fprintf(stderr, "[Motor] cannot open /dev/tmi8152\n"); return false; }

    struct chx_mode mode;
    mode.chx = toChannel(dir);
    mode.mode = AOTU_CTRL;
    ioctl(fd, SET_MODE, &mode);

    struct chx_enable config;
    std::memset(&config, 0, sizeof(config));
    config.chx       = toChannel(dir);
    config.subdivide = subdivide;
    config.direct    = direct;
    config.phase     = 0;
    config.cycles    = cycles;
    config.speed     = speed;

    if (ioctl(fd, CHAN_START_TEST, &config) < 0) {
        std::fprintf(stderr, "[Motor] ioctl CHAN_START_TEST failed\n");
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

void MotorController::stop(MotorDirection dir) {
    if (!initialized_) return;
    struct chx_enable params;
    std::memset(&params, 0, sizeof(params));
    params.chx = toChannel(dir);
    params.direct = STOP;
    motor_hal_stop(&params);
}

float MotorController::getPosition(MotorDirection dir) {
    if (!initialized_) return 0.0f;
    struct get_angle angle;
    angle.chx = toChannel(dir);
    float pos = 0.0f;
    motor_hal_get_position(&angle, &pos);
    return pos;
}

bool MotorController::isInitialized() const {
    return initialized_;
}

} // namespace ft
