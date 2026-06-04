#include "tests/ITestModule.h"
#include "platforms/common/interface/IHallDriver.h"

namespace ft {

class HallTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto hall = ctx.create<IHallDriver>();
        if (!hall)
            return TestResult::skipped("no hall driver");
        if (!hall->init())
            return TestResult::fail("hall init failed");
        hall->readValue();
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("hall", HallTest);

} // namespace ft
