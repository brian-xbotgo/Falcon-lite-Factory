#include "tests/ITestModule.h"

namespace ft {

class SocTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("soc", SocTest);

} // namespace ft
