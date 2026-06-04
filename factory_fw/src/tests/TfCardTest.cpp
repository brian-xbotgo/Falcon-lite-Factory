#include "tests/ITestModule.h"
#include "common/ShellUtils.h"

namespace ft {

class TfCardTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        auto out = shell_exec("lsblk | grep mmc 2>/dev/null");
        if (out.empty())
            return TestResult::fail("no tf card detected");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("tfcard", TfCardTest);

} // namespace ft
