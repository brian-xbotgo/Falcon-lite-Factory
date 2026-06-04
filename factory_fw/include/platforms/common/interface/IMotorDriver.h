#pragma once
#include <cstdint>

namespace ft {

enum MotorDirection { MOTOR_HORIZONTAL = 0, MOTOR_VERTICAL = 1 };
enum MotorSpeed { SPEED_LOW = 0, SPEED_MID, SPEED_HIGH };
enum MotorDirect { DIRECT_FORWARD = 0, DIRECT_BACKWARD };

class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;
    virtual bool move(MotorDirection dir, float angleDeg,
                      MotorSpeed speed, MotorDirect direct) = 0;
    virtual bool startBoardTest(MotorDirection dir, unsigned int cycles,
                                int subdivide, MotorSpeed speed, MotorDirect direct) = 0;
    virtual void stop(MotorDirection dir) = 0;
    virtual float getPosition(MotorDirection dir) = 0;
    virtual bool isInitialized() const = 0;
};

} // namespace ft
