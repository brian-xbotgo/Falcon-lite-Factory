#include "tests/ITestModule.h"
#include "control/StatusLedController.h"
#include "platforms/common/interface/IMotorDriver.h"
#include "platforms/common/interface/IRecorder.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ft {

namespace {

std::mutex g_recorderMutex;
std::unique_ptr<IRecorder> g_agingRecorder;
std::mutex g_motorMutex;
std::atomic<bool> g_agingMotorRunning{false};
std::thread g_verticalMotorThread;
std::thread g_horizontalMotorThread;

TestResult resultWithCode(bool pass, uint32_t errorCode, const std::string& detail)
{
    auto result = pass ? TestResult::pass() : TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

void agingMotorLoop(std::unique_ptr<IMotorDriver> motor, MotorDirection direction)
{
    if (!motor || !motor->init()) {
        std::fprintf(stderr, "[AgingTest] motor init failed direction=%d\n", direction);
        return;
    }

    std::fprintf(stderr, "[AgingTest] motor loop started direction=%d\n", direction);
    while (g_agingMotorRunning.load()) {
        if (direction == MOTOR_VERTICAL) {
            motor->move(direction, 75.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!g_agingMotorRunning.load()) {
                break;
            }
            motor->move(direction, 150.0f, SPEED_MID, DIRECT_BACKWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_agingMotorRunning.load()) {
                break;
            }
            motor->move(direction, 75.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } else {
            motor->move(direction, 90.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_agingMotorRunning.load()) {
                break;
            }
            motor->move(direction, 180.0f, SPEED_MID, DIRECT_BACKWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_agingMotorRunning.load()) {
                break;
            }
            motor->move(direction, 90.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    motor->stop(direction);
    motor->deinit();
    std::fprintf(stderr, "[AgingTest] motor loop stopped direction=%d\n", direction);
}

bool startAgingMotors(TestContext& ctx)
{
    std::lock_guard<std::mutex> lock(g_motorMutex);
    if (g_agingMotorRunning.exchange(true)) {
        std::fprintf(stderr, "[AgingTest] motor loops already running\n");
        return true;
    }

    if (g_verticalMotorThread.joinable()) {
        g_verticalMotorThread.join();
    }
    if (g_horizontalMotorThread.joinable()) {
        g_horizontalMotorThread.join();
    }

    g_verticalMotorThread =
        std::thread(agingMotorLoop, ctx.create<IMotorDriver>(), MOTOR_VERTICAL);
    g_horizontalMotorThread =
        std::thread(agingMotorLoop, ctx.create<IMotorDriver>(), MOTOR_HORIZONTAL);
    return true;
}

void stopAgingMotors()
{
    std::lock_guard<std::mutex> lock(g_motorMutex);
    g_agingMotorRunning = false;
    if (g_verticalMotorThread.joinable()) {
        g_verticalMotorThread.join();
    }
    if (g_horizontalMotorThread.joinable()) {
        g_horizontalMotorThread.join();
    }
}

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

void writeTextFile(const char* path, const char* text)
{
    FILE* f = std::fopen(path, "w");
    if (!f) {
        return;
    }
    std::fputs(text, f);
    std::fclose(f);
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
            writeTextFile("/userdata/aging_completed.txt", "2\n");
            const bool motorsOk = startAgingMotors(ctx);
            auto rec = startAgingRecorder(ctx);
            const bool pass = motorsOk && rec.status == TestResult::Status::Pass;
            auto result = resultWithCode(pass, pass ? 0 : 1,
                                         pass ? "full aging started" : "aging start failed");
            result.data["recording"] = rec.data.value("recording", false);
            result.data["motors"] = motorsOk;
            return result;
        }
        if (topic == "32R") {
            writeTextFile("/userdata/aging_completed.txt", "2\n");
            const bool motorsOk = startAgingMotors(ctx);
            return resultWithCode(motorsOk, motorsOk ? 0 : 1,
                                  motorsOk ? "motor aging started" : "motor aging start failed");
        }
        if (topic == "34R" || topic == "36R") {
            stopAgingMotors();
            writeTextFile("/userdata/aging_completed.txt", topic == "34R" ? "1\n" : "30\n");
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
