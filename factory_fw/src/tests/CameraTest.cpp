#include "tests/ITestModule.h"

namespace ft {

class CameraTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        return TestResult::skipped("not implemented");
    }
};

REGISTER_TEST_MODULE("camera", CameraTest);

} // namespace ft
