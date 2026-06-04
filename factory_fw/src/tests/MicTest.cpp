#include "tests/ITestModule.h"

namespace ft {

class MicTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        // TODO: implement mic sensitivity + THD test
        return TestResult::skipped("mic test not yet implemented");
    }
};

REGISTER_TEST_MODULE("mic", MicTest);

} // namespace ft
