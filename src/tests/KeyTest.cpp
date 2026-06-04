#include "tests/ITestModule.h"

namespace ft {

class KeyTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        // TODO: implement key detection via GPIO or input event
        return TestResult::skipped("key test not yet implemented");
    }
};

REGISTER_TEST_MODULE("key", KeyTest);

} // namespace ft
