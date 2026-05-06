#pragma once

#include <string>
#include <atomic>
#include "hal/MotorHal.h"

namespace ft {

// 电机方向: HORIZONTAL = CH12, VERTICAL = CH34
enum MotorDirection { MOTOR_HORIZONTAL = 0, MOTOR_VERTICAL = 1 };

class MotorController {
public:
    static MotorController& instance();

    bool init();
    void deinit();

    // 角度转 1/100 度整数（MotorHal 用的单位）
    bool start(MotorDirection dir, float angleDeg, enum motor_speed_gear speed, enum direct_select direct);
    bool startBoardTest(MotorDirection dir, unsigned int cycles, enum subdivide_select subdivide, enum motor_speed_gear speed, enum direct_select direct);
    void stop(MotorDirection dir);
    float getPosition(MotorDirection dir);
    bool isInitialized() const;

private:
    MotorController() = default;
    static enum channel_select toChannel(MotorDirection dir);
    static enum direct_select angleToDirect(float angleDeg);

    std::atomic<bool> initialized_{false};
};

} // namespace ft
