#pragma once
#include "platforms/common/interface/IBatteryDriver.h"

namespace ft {

class Cw221xBatteryDriver : public IBatteryDriver {
public:
    bool init() override { return true; }
    BatteryInfo read() override {
        return BatteryInfo{3.85, 85, "CW221X", true};
    }
    std::string getSysfsPath() const override {
        return "/sys/class/power_supply/cw221X-bat/uevent";
    }
};

} // namespace ft
