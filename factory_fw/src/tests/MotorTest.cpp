#include "tests/ITestModule.h"

namespace ft {

class MotorTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("motor", MotorTest);

} // namespace ft
