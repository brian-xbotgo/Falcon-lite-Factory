#include "tests/ITestModule.h"

namespace ft {

class WifiTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("wifi", WifiTest);

} // namespace ft
