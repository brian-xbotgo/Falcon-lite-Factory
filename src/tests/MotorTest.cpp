#include "tests/ITestModule.h"
#include "platforms/common/interface/IMotorDriver.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ft {

namespace {

std::atomic<bool> gVerticalMotorLoop{false};
std::atomic<bool> gHorizontalMotorLoop{false};

constexpr uint32_t kMotorInitFail = 1u << 0;
constexpr uint32_t kMotorStartFail = 1u << 1;

TestResult resultWithCode(bool pass, uint32_t errorCode, const std::string& detail)
{
    auto result = pass ? TestResult::pass() : TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

MotorDirection parseDirection(const std::string& value)
{
    if (value == "vertical") {
        return MOTOR_VERTICAL;
    }
    return MOTOR_HORIZONTAL;
}

MotorDirect parseDirect(const std::string& value)
{
    if (value == "backward" || value == "back") {
        return DIRECT_BACKWARD;
    }
    return DIRECT_FORWARD;
}

std::atomic<bool>& loopFlag(MotorDirection direction)
{
    return direction == MOTOR_VERTICAL ? gVerticalMotorLoop : gHorizontalMotorLoop;
}

const char* directionName(MotorDirection direction)
{
    return direction == MOTOR_VERTICAL ? "vertical" : "horizontal";
}

void runLoop(std::unique_ptr<IMotorDriver> motor, MotorDirection direction)
{
    auto& running = loopFlag(direction);
    if (!motor || !motor->init()) {
        std::fprintf(stderr, "[MotorTest] loop init failed direction=%s\n",
                     directionName(direction));
        running = false;
        return;
    }

    std::fprintf(stderr, "[MotorTest] loop started direction=%s\n",
                 directionName(direction));
    while (running.load()) {
        if (direction == MOTOR_VERTICAL) {
            motor->move(direction, 360.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running.load()) {
                break;
            }
            motor->move(direction, 360.0f, SPEED_MID, DIRECT_BACKWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running.load()) {
                break;
            }
            motor->move(direction, 75.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        } else {
            motor->move(direction, 90.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running.load()) {
                break;
            }
            motor->move(direction, 180.0f, SPEED_MID, DIRECT_BACKWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running.load()) {
                break;
            }
            motor->move(direction, 90.0f, SPEED_MID, DIRECT_FORWARD);
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    motor->stop(direction);
    motor->deinit();
    std::fprintf(stderr, "[MotorTest] loop stopped direction=%s\n",
                 directionName(direction));
}

} // namespace

class MotorTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const auto mode = ctx.params().value("mode", std::string("board"));
        const auto direction = parseDirection(ctx.params().value("direction", std::string("horizontal")));

        if (mode == "stop") {
            loopFlag(direction) = false;
            auto motor = ctx.create<IMotorDriver>();
            if (motor && motor->init()) {
                motor->stop(direction);
                motor->deinit();
            }
            std::fprintf(stderr, "[MotorTest] stop direction=%s\n",
                         directionName(direction));
            return resultWithCode(true, 0, "");
        }

        if (mode == "loop") {
            auto& running = loopFlag(direction);
            if (running.exchange(true)) {
                std::fprintf(stderr, "[MotorTest] loop already running direction=%s\n",
                             directionName(direction));
                return resultWithCode(true, 0, "");
            }
            std::thread(runLoop, ctx.create<IMotorDriver>(), direction).detach();
            return resultWithCode(true, 0, "");
        }

        auto motor = ctx.create<IMotorDriver>();
        if (!motor)
            return resultWithCode(false, kMotorInitFail, "no motor driver");
        if (!motor->init())
            return resultWithCode(false, kMotorInitFail, "motor init failed");

        const int cycles = ctx.params().value("cycles", 1024);
        const int subdivide = ctx.params().value("subdivide", 128);
        const auto direct = parseDirect(ctx.params().value("direct", std::string("forward")));
        std::fprintf(stderr,
                     "[MotorTest] board direction=%s cycles=%d subdivide=%d direct=%s\n",
                     directionName(direction), cycles, subdivide,
                     direct == DIRECT_FORWARD ? "forward" : "backward");

        const bool ok = motor->startBoardTest(direction, static_cast<unsigned int>(cycles),
                                              subdivide, SPEED_MID, direct);
        motor->deinit();
        return resultWithCode(ok, ok ? 0 : kMotorStartFail,
                              ok ? "" : "motor start failed");
    }
};

REGISTER_TEST_MODULE("motor", MotorTest);

} // namespace ft
