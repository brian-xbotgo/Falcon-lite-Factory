#include "tests/ITestModule.h"
#include "platforms/common/interface/IWifiManager.h"

namespace ft {

class WifiTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto wifi = ctx.create<IWifiManager>();
        if (!wifi)
            return TestResult::skipped("no wifi driver");
        auto scan = wifi->scanWifi();
        if (scan.empty())
            return TestResult::fail("wifi scan empty");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("wifi", WifiTest);

} // namespace ft
