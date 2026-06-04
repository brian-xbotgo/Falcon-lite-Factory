#include "core/TestEngine.h"
#include "core/ModuleRegistry.h"
#include "core/TestResult.h"
#include "core/TestContext.h"
#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>

namespace ft {

TestEngine::TestEngine(const DriverRegistry& drivers, const PlatformConfig& cfg)
    : drivers_(drivers), config_(cfg) {}

TestEngine::~TestEngine() { stop(); }

bool TestEngine::loadTestConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[TestEngine] cannot open %s\n", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        f >> j;
        if (!j.contains("tests") || !j.at("tests").is_array()) {
            fprintf(stderr, "[TestEngine] tests.json missing or invalid 'tests' array\n");
            return false;
        }
        for (auto& t : j.at("tests")) {
            auto name = t.at("module").get<std::string>();
            if (!t.contains("topic")) {
                fprintf(stderr, "[TestEngine] tests.json entry missing 'topic' for module: %s\n",
                        name.c_str());
                return false;
            }
            if (!ModuleRegistry::instance().has(name)) {
                fprintf(stderr, "[TestEngine] tests.json references unknown module: %s\n",
                        name.c_str());
                return false;
            }
        }
        testConfigs_ = std::move(j);
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[TestEngine] parse error: %s\n", e.what());
        return false;
    }
}

void TestEngine::run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TestEngine::stop() { running_ = false; }

void TestEngine::onMqttMessage(const std::string& topic, const std::string& payload) {
    try {
        nlohmann::json j = nlohmann::json::parse(payload);
        // If payload is a full test config, use it directly
        if (j.contains("module")) {
            dispatch(topic, j);
            return;
        }
    } catch (...) {
        // Not a JSON payload, ignore
    }

    // Lookup testCfg by topic from tests.json
    try {
        for (auto& t : testConfigs_.at("tests")) {
            if (t.at("topic").get<std::string>() == topic) {
                if (!t.value("enabled", true)) {
                    publishResult(topic, TestResult::skipped("test disabled"));
                    return;
                }
                dispatch(topic, t);
                return;
            }
        }
        publishResult(topic, TestResult::fail("topic not found in tests.json"));
    } catch (const std::exception& e) {
        publishResult(topic, TestResult::fail(std::string("lookup error: ") + e.what()));
    }
}

void TestEngine::dispatch(const std::string& topic, const nlohmann::json& testCfg) {
    std::unique_ptr<ITestModule> mod;
    try {
        mod = ModuleRegistry::instance().create(
            testCfg.at("module").get<std::string>());
    } catch (const std::exception& e) {
        publishResult(topic,
            TestResult::fail(std::string("bad config: ") + e.what()));
        return;
    }

    if (!mod) {
        publishResult(topic, TestResult::fail("unknown module"));
        return;
    }

    TestContext ctx(drivers_, config_,
                    testCfg.value("params", nlohmann::json::object()));

    // shared_ptr makes the lambda copy-constructible (required by std::function)
    auto modShared = std::shared_ptr<ITestModule>(std::move(mod));
    auto task = [modShared, ctx = std::move(ctx),
                 topic, this]() mutable -> TestResult {
        try {
            return modShared->run(ctx);
        } catch (const std::exception& e) {
            return TestResult::fail(std::string("exception: ") + e.what());
        } catch (...) {
            return TestResult::fail("unknown exception");
        }
    };

    if (testCfg.value("async", false)) {
        asyncQueue_.enqueue([task = std::move(task), topic, this]() mutable {
            publishResult(topic, task());
        });
    } else {
        publishResult(topic, asyncQueue_.enqueueAndWait(std::move(task)));
    }
}

void TestEngine::publishResult(const std::string& topic, const TestResult& result) {
    std::lock_guard<std::mutex> lock(mqttMutex_);
    const char* statusStr = "UNKNOWN";
    switch (result.status) {
        case TestResult::Status::Pass:    statusStr = "PASS"; break;
        case TestResult::Status::Fail:    statusStr = "FAIL"; break;
        case TestResult::Status::Skipped: statusStr = "SKIP"; break;
    }
    fprintf(stdout, "[Result] topic=%s status=%s detail=%s\n",
            topic.c_str(), statusStr, result.detail.c_str());
    fflush(stdout);
    // TODO: mosquitto_publish with result JSON
}

} // namespace ft
