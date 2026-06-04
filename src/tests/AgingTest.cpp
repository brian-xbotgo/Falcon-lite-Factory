#include "tests/ITestModule.h"

namespace ft {

class AgingTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        // TODO: implement aging stress test (GPU/NPU/thermal cycling)
        return TestResult::skipped("aging test not yet implemented");
    }
};

REGISTER_TEST_MODULE("aging", AgingTest);

} // namespace ft
