#pragma once
#include "platforms/common/interface/IMotorDriver.h"

namespace ft {

class Tmi8152MotorDriver : public IMotorDriver {
public:
    bool init() override;
    void deinit() override;
    bool move(MotorDirection dir, float angleDeg,
              MotorSpeed speed, MotorDirect direct) override;
    bool startBoardTest(MotorDirection dir, unsigned int cycles, int subdivide,
                        MotorSpeed speed, MotorDirect direct) override;
    void stop(MotorDirection dir) override;
    float getPosition(MotorDirection dir) override;
    bool isInitialized() const override { return initialized_; }

private:
    int fd_ = -1;
    bool initialized_ = false;
};

} // namespace ft
