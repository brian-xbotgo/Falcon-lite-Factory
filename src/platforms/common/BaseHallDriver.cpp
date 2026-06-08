#include "platforms/common/interface/IHallDriver.h"

namespace ft {

class BaseHallDriver : public IHallDriver {
public:
    bool init() override { return false; }
    void deinit() override {}
    float readValue() override { return 0.0f; }
    bool isInitialized() const override { return false; }
};

} // namespace ft
