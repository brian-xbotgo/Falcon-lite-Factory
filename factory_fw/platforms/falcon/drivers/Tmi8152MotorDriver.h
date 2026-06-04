#pragma once
#include "platforms/common/interface/IMotorDriver.h"

namespace ft {

class Tmi8152MotorDriver : public IMotorDriver {
public:
    bool init() override { initialized_ = true; return true; }
    void deinit() override { initialized_ = false; }
    bool move(MotorDirection, float, MotorSpeed, MotorDirect) override { return true; }
    bool startBoardTest(MotorDirection, unsigned int, int, MotorSpeed, MotorDirect) override {
        return true;
    }
    void stop(MotorDirection) override {}
    float getPosition(MotorDirection) override { return 0.0f; }
    bool isInitialized() const override { return initialized_; }
private:
    bool initialized_ = false;
};

} // namespace ft
