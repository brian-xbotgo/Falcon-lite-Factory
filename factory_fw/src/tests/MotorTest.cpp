#include "tests/ITestModule.h"
#include "platforms/common/interface/IMotorDriver.h"

namespace ft {

class MotorTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto motor = ctx.create<IMotorDriver>();
        if (!motor)
            return TestResult::skipped("no motor driver");
        if (!motor->init())
            return TestResult::fail("motor init failed");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("motor", MotorTest);

} // namespace ft
