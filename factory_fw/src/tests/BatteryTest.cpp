#include "tests/ITestModule.h"

namespace ft {

class BatteryTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("battery", BatteryTest);

} // namespace ft
