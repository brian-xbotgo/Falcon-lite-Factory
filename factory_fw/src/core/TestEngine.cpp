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

#ifdef HAVE_MOSQUITTO
#include "mosquitto/mosquitto.h"
#endif

namespace ft {

#ifdef HAVE_MOSQUITTO
static void onMqttMessageCallback(struct mosquitto*, void* userdata,
                                   const struct mosquitto_message* msg)
{
    auto* engine = static_cast<TestEngine*>(userdata);
    if (engine && msg && msg->payload) {
        std::string topic(msg->topic);
        std::string payload(static_cast<char*>(msg->payload),
                            static_cast<size_t>(msg->payloadlen));
        engine->onMqttMessage(topic, payload);
    }
}
#endif

TestEngine::TestEngine(const DriverRegistry& drivers, const PlatformConfig& cfg)
    : drivers_(drivers), config_(cfg)
{
#ifdef HAVE_MOSQUITTO
    mosquitto_lib_init();
    mqttClient_ = mosquitto_new("factory_test", true, this);
    if (mqttClient_) {
        mosquitto_message_callback_set(mqttClient_, onMqttMessageCallback);
    } else {
        std::fprintf(stderr, "[TestEngine] mosquitto_new failed\n");
    }
#endif
}

TestEngine::~TestEngine()
{
    stop();
#ifdef HAVE_MOSQUITTO
    if (mqttClient_) {
        mosquitto_destroy(mqttClient_);
        mqttClient_ = nullptr;
    }
    mosquitto_lib_cleanup();
#endif
}

bool TestEngine::loadTestConfig(const std::string& path)
{
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

bool TestEngine::connectMqtt()
{
#ifdef HAVE_MOSQUITTO
    if (!mqttClient_) return false;

    std::string host = "127.0.0.1";
    int port = 1883;

    try {
        const auto& raw = config_.raw();
        if (raw.contains("mqtt")) {
            const auto& mqtt = raw["mqtt"];
            if (mqtt.contains("host")) host = mqtt["host"].get<std::string>();
            if (mqtt.contains("port")) port = mqtt["port"].get<int>();
        }
    } catch (...) {
        // use defaults
    }

    int rc = mosquitto_connect(mqttClient_, host.c_str(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[TestEngine] mosquitto_connect failed: %s\n",
                     mosquitto_strerror(rc));
        return false;
    }

    // Subscribe all topics from tests.json
    if (testConfigs_.contains("tests") && testConfigs_["tests"].is_array()) {
        for (auto& t : testConfigs_["tests"]) {
            if (t.contains("topic")) {
                std::string topic = t["topic"].get<std::string>();
                mosquitto_subscribe(mqttClient_, nullptr, topic.c_str(), 0);
            }
        }
    }
    return true;
#else
    return false;
#endif
}

void TestEngine::run()
{
#ifdef HAVE_MOSQUITTO
    if (mqttClient_ && connectMqtt()) {
        int rc = mosquitto_loop_start(mqttClient_);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::fprintf(stderr, "[TestEngine] mosquitto_loop_start failed: %s\n",
                         mosquitto_strerror(rc));
        }
    }
#endif
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TestEngine::stop()
{
    running_ = false;
#ifdef HAVE_MOSQUITTO
    if (mqttClient_) {
        mosquitto_loop_stop(mqttClient_, true);
        mosquitto_disconnect(mqttClient_);
    }
#endif
}

void TestEngine::onMqttMessage(const std::string& topic, const std::string& payload)
{
    try {
        nlohmann::json j = nlohmann::json::parse(payload);
        if (j.contains("module")) {
            dispatch(topic, j);
            return;
        }
    } catch (...) {
        // Not a JSON payload, ignore
    }

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

void TestEngine::dispatch(const std::string& topic, const nlohmann::json& testCfg)
{
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

void TestEngine::publishResult(const std::string& topic, const TestResult& result)
{
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

#ifdef HAVE_MOSQUITTO
    if (mqttClient_) {
        try {
            nlohmann::json j = {
                {"topic", topic},
                {"status", statusStr},
                {"detail", result.detail}
            };
            std::string payload = j.dump();
            mosquitto_publish(mqttClient_, nullptr, topic.c_str(),
                              static_cast<int>(payload.size()),
                              payload.c_str(), 0, false);
        } catch (...) {
            // JSON serialization failure, ignore
        }
    }
#endif
}

} // namespace ft
