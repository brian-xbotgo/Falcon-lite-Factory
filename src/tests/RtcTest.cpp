#include "tests/ITestModule.h"
#include "common/ShellUtils.h"

namespace ft {

class RtcTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        auto out = shell_exec("hwclock -r 2>/dev/null");
        if (out.find("1970") != std::string::npos || out.empty())
            return TestResult::fail("rtc not set");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("rtc", RtcTest);

} // namespace ft
