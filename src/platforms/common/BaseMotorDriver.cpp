#include "platforms/common/interface/IMotorDriver.h"

namespace ft {

class BaseMotorDriver : public IMotorDriver {
public:
    bool init() override { return false; }
    void deinit() override {}
    bool move(MotorDirection, float, MotorSpeed, MotorDirect) override { return false; }
    bool startBoardTest(MotorDirection, unsigned int, int, MotorSpeed, MotorDirect) override { return false; }
    void stop(MotorDirection) override {}
    float getPosition(MotorDirection) override { return 0.0f; }
    bool isInitialized() const override { return false; }
};

} // namespace ft
