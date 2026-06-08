#include "platforms/common/interface/IBatteryDriver.h"

namespace ft {

class BaseBatteryDriver : public IBatteryDriver {
public:
    bool init() override { return false; }
    BatteryInfo read() override { return {}; }
    std::string getSysfsPath() const override { return ""; }
};

} // namespace ft
