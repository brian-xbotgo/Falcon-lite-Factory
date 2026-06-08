#include "platforms/common/interface/IDisplayDriver.h"

namespace ft {

class BaseDisplayDriver : public IDisplayDriver {
public:
    bool init() override { return false; }
    void deinit() override {}
    uint32_t taskHandler() override { return 5; }
    void setBatteryPercent(int) override {}
    void setBatteryModel(const char*) override {}
    void setKeyValid(bool) override {}
    bool isInitialized() const override { return false; }
};

} // namespace ft
