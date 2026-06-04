#include "tests/ITestModule.h"
#include "platforms/common/interface/ICameraDriver.h"

namespace ft {

class CameraTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto cam = ctx.create<ICameraDriver>();
        if (!cam)
            return TestResult::skipped("no camera driver");
        int idx = ctx.params().value("cam_index", 0);
        auto result = cam->probe(idx);
        if (!result.found)
            return TestResult::fail("camera not found");
        if (!result.otpValid)
            return TestResult::fail("otp invalid");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("camera", CameraTest);

} // namespace ft
