#include "tests/ITestModule.h"

namespace ft {

class MicTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("mic", MicTest);

} // namespace ft
