#include "tests/ITestModule.h"

namespace ft {

class HallTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("hall", HallTest);

} // namespace ft
