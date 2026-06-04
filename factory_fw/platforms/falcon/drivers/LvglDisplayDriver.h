#pragma once
#include "platforms/common/interface/IDisplayDriver.h"

namespace ft {

class LvglDisplayDriver : public IDisplayDriver {
public:
    bool init() override { initialized_ = true; return true; }
    void deinit() override { initialized_ = false; }
    uint32_t taskHandler() override { return 0; }
    void setBatteryPercent(int) override {}
    void setBatteryModel(const char*) override {}
    void setKeyValid(bool) override {}
    bool isInitialized() const override { return initialized_; }
private:
    bool initialized_ = false;
};

} // namespace ft
