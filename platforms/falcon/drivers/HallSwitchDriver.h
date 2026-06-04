#pragma once
#include "platforms/common/interface/IHallDriver.h"

namespace ft {

class HallSwitchDriver : public IHallDriver {
public:
    bool init() override { initialized_ = true; return true; }
    void deinit() override { initialized_ = false; }
    float readValue() override { return 0.0f; }
    bool isInitialized() const override { return initialized_; }
private:
    bool initialized_ = false;
};

} // namespace ft
