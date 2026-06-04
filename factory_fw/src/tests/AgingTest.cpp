#include "tests/ITestModule.h"

namespace ft {

class AgingTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("aging", AgingTest);

} // namespace ft
