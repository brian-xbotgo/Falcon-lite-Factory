#include "tests/ITestModule.h"
#include "common/ShellUtils.h"

namespace ft {

class SocTest : public ITestModule {
public:
    TestResult run(TestContext&) override {
        auto cpu = shell_exec("cat /proc/cpuinfo 2>/dev/null");
        auto temp = shell_exec("cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null");
        if (cpu.empty() || cpu.find("processor") == std::string::npos)
            return TestResult::fail("cannot read cpuinfo");
        auto result = TestResult::pass();
        result.data["cpuinfo_readable"] = true;
        result.data["temp"] = temp;
        return result;
    }
};

REGISTER_TEST_MODULE("soc", SocTest);

} // namespace ft
