#include "tests/ITestModule.h"
#include "control/StatusLedController.h"
#include "platforms/common/interface/IRecorder.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

namespace ft {

namespace {

std::mutex g_recorderMutex;
std::unique_ptr<IRecorder> g_agingRecorder;

TestResult startAgingRecorder(TestContext& ctx)
{
    std::lock_guard<std::mutex> lock(g_recorderMutex);

    if (!g_agingRecorder) {
        g_agingRecorder = ctx.create<IRecorder>();
    }

    if (!g_agingRecorder) {
        return TestResult::fail("no recorder driver");
    }

    if (g_agingRecorder->isRecording()) {
        auto result = TestResult::pass();
        result.detail = "aging recorder already running";
        result.data["recording"] = true;
        return result;
    }

    RecorderCmd cmd{};
    const bool ok = g_agingRecorder->start(cmd);
    std::fprintf(stderr, "[AgingTest] recorder_start result=%s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        return TestResult::fail("aging recorder start failed");
    }

    auto result = TestResult::pass();
    result.detail = "aging recorder started";
    result.data["recording"] = true;
    return result;
}

TestResult stopAgingRecorder()
{
    std::lock_guard<std::mutex> lock(g_recorderMutex);

    if (!g_agingRecorder || !g_agingRecorder->isRecording()) {
        auto result = TestResult::pass();
        result.detail = "aging recorder already stopped";
        result.data["recording"] = false;
        return result;
    }

    const bool ok = g_agingRecorder->stop();
    std::fprintf(stderr, "[AgingTest] recorder_stop result=%s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        return TestResult::fail("aging recorder stop failed");
    }

    auto result = TestResult::pass();
    result.detail = "aging recorder stopped";
    result.data["recording"] = false;
    return result;
}

} // namespace

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

        if (topic == "30R") {
            return startAgingRecorder(ctx);
        }
        if (topic == "34R" || topic == "36R") {
            return stopAgingRecorder();
        }

        auto result = TestResult::pass();
        result.detail = topic == "32R"
            ? "motor aging mode set"
            : "status led mode set; aging stress pending";
        return result;
    }
};

REGISTER_TEST_MODULE("aging", AgingTest);

} // namespace ft
