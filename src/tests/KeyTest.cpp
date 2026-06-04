#include "tests/ITestModule.h"

namespace ft {

class KeyTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("key", KeyTest);

} // namespace ft
