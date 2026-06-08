#include "tests/ITestModule.h"
#include "control/StatusLedController.h"

#include <cstdio>
#include <string>

namespace ft {

class AgingTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const auto topic = ctx.params().value("topic", std::string());
        StatusLedMode mode = StatusLedMode::Test;

        if (topic == "34R") {
            mode = StatusLedMode::Normal;
        } else if (topic == "36R") {
            mode = StatusLedMode::TestFail;
        } else if (topic == "30R" || topic == "32R" || topic.empty()) {
            mode = StatusLedMode::Test;
        }

        const bool ok = StatusLedController::instance().setMode(mode);
        std::fprintf(stderr, "[AgingTest] topic=%s led_mode=%s result=%s\n",
                     topic.empty() ? "<unknown>" : topic.c_str(),
                     statusLedModeName(mode),
                     ok ? "PASS" : "FAIL");

        if (!ok) {
            return TestResult::fail("status led control failed");
        }

        auto result = TestResult::pass();
        result.detail = "status led mode set; aging stress pending";
        return result;
    }
};

REGISTER_TEST_MODULE("aging", AgingTest);

} // namespace ft
