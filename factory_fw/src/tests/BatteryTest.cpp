#include "tests/ITestModule.h"
#include "platforms/common/interface/IBatteryDriver.h"

namespace ft {

class BatteryTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto bat = ctx.create<IBatteryDriver>();
        if (!bat)
            return TestResult::skipped("no battery driver");
        if (!bat->init())
            return TestResult::fail("battery init failed");
        auto info = bat->read();
        if (!info.valid)
            return TestResult::fail("battery read invalid");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("battery", BatteryTest);

} // namespace ft
