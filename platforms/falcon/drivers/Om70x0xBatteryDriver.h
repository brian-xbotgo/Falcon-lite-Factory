#pragma once
#include "platforms/common/interface/IBatteryDriver.h"
#include <string>

namespace ft {

class Om70x0xBatteryDriver : public IBatteryDriver {
public:
    bool init() override;
    BatteryInfo read() override;
    std::string getSysfsPath() const override {
        return "/sys/class/power_supply/om70X0X-bat/uevent";
    }

private:
    static std::string ueventValue(const std::string& content,
                                   const std::string& key);
};

} // namespace ft
