#include "tests/ITestModule.h"

namespace ft {

class SysTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("sys", SysTest);

} // namespace ft
