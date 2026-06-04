#include "tests/ITestModule.h"
#include "common/ShellUtils.h"
#include <cstdio>

namespace ft {

class SysTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        auto ver = shell_exec("cat /etc/version 2>/dev/null");
        if (ver.empty())
            return TestResult::fail("cannot read version");
        auto result = TestResult::pass();
        result.data["version"] = ver;
        return result;
    }
};

REGISTER_TEST_MODULE("sys", SysTest);

} // namespace ft
