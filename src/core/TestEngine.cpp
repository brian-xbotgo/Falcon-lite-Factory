#include "core/TestEngine.h"
#include "core/ModuleRegistry.h"
#include "core/TestResult.h"
#include "core/TestContext.h"
#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

#ifdef HAVE_MOSQUITTO
#include <mosquitto.h>
#endif

namespace ft {

namespace {

constexpr size_t kRequestPayloadSize = 38;
constexpr size_t kResponsePayloadSize = 42;

std::string answerTopicFor(const std::string& requestTopic)
{
    if (!requestTopic.empty() && requestTopic.back() == 'R') {
        std::string answer = requestTopic;
        answer.back() = 'A';
        return answer;
    }
    return requestTopic + "A";
}

uint32_t errorCodeFromResult(const TestResult& result)
{
    try {
        if (result.data.is_object() && result.data.contains("error_code")) {
            return result.data.at("error_code").get<uint32_t>();
        }
    } catch (...) {
        // Fall through to the protocol-level generic failure bit.
    }

    if (result.status == TestResult::Status::Pass) {
        return 0;
    }

    return 1;
}

std::string trimProtocolText(std::string value)
{
    const auto nul = value.find('\0');
    if (nul != std::string::npos) {
        value.resize(nul);
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

bool isValidProtocolSn(const std::string& sn)
{
    if (sn.size() != 14) {
        return false;
    }

    bool hasNonZero = false;
    for (unsigned char ch : sn) {
        if (!std::isalnum(ch)) {
            return false;
        }
        if (ch != '0') {
            hasNonZero = true;
        }
    }
    return hasNonZero;
}

std::string readValidSnFile(const char* path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }

    std::string value;
    std::getline(in, value);
    value = trimProtocolText(value);
    return isValidProtocolSn(value) ? value : std::string();
}

std::string responseSnForPayload(const std::string& requestPayload)
{
    if (requestPayload.size() >= 14) {
        const auto payloadSn = trimProtocolText(requestPayload.substr(0, 14));
        if (isValidProtocolSn(payloadSn)) {
            return payloadSn;
        }
    }

    const auto fileSn = readValidSnFile("/device_data/pcba.txt");
    if (!fileSn.empty()) {
        return fileSn;
    }

    return "00000000000000";
}

std::string protocolResponsePayload(const std::string& requestPayload,
                                    uint32_t errorCode,
                                    const std::string& responseExtra)
{
    std::string payload(kResponsePayloadSize, '\0');
    const size_t copyLen = std::min(requestPayload.size(), kRequestPayloadSize);
    std::copy_n(requestPayload.data(), copyLen, payload.data());

    const auto sn = responseSnForPayload(requestPayload);
    std::copy(sn.begin(), sn.end(), payload.begin());

    payload[38] = static_cast<char>((errorCode >> 24) & 0xff);
    payload[39] = static_cast<char>((errorCode >> 16) & 0xff);
    payload[40] = static_cast<char>((errorCode >> 8) & 0xff);
    payload[41] = static_cast<char>(errorCode & 0xff);
    payload.append(responseExtra);
    return payload;
}

void persistSnFromPayload(const std::string& requestPayload)
{
    if (requestPayload.size() < kRequestPayloadSize) {
        return;
    }

    const auto sn = trimProtocolText(requestPayload.substr(0, 14));
    if (!isValidProtocolSn(sn)) {
        return;
    }

    constexpr const char* kDeviceDataDir = "/device_data";
    constexpr const char* kPcbaSnPath = "/device_data/pcba.txt";
    mkdir(kDeviceDataDir, 0755);

    std::ofstream out(kPcbaSnPath, std::ios::trunc);
    if (out.is_open()) {
        out << sn << '\n';
    }
}

} // namespace

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
    mqttClient_ = mosquitto_new("prodTest", true, this);
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
    std::fprintf(stderr, "[TestEngine] MQTT connected %s:%d\n", host.c_str(), port);

    // Subscribe all topics from tests.json
    if (testConfigs_.contains("tests") && testConfigs_["tests"].is_array()) {
        for (auto& t : testConfigs_["tests"]) {
            if (t.contains("topic")) {
                std::string topic = t["topic"].get<std::string>();
                rc = mosquitto_subscribe(mqttClient_, nullptr, topic.c_str(), 2);
                std::fprintf(stderr, "[TestEngine] subscribe %s qos=2 rc=%d\n",
                             topic.c_str(), rc);
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
        } else {
            std::fprintf(stderr, "[TestEngine] mosquitto loop started\n");
        }
    } else {
        std::fprintf(stderr, "[TestEngine] MQTT disabled or connect failed\n");
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
    std::fprintf(stderr, "[TestEngine] received topic=%s payload_len=%zu\n",
                 topic.c_str(), payload.size());
    std::fflush(stderr);

    // 永远走 asyncQueue：mosquitto 回调线程只做"入队"，不阻塞，
    // 避免 sync 测试卡住 MQTT 网络 I/O 导致 keepalive 超时。
    asyncQueue_.enqueue([this, topic, payload]() {
        try {
            nlohmann::json j = nlohmann::json::parse(payload);
            if (j.contains("module")) {
                dispatch(topic, j, payload);
                return;
            }
        } catch (...) {
            persistSnFromPayload(payload);
        }

        try {
            for (auto& t : testConfigs_.at("tests")) {
                if (t.at("topic").get<std::string>() == topic) {
                    if (!t.value("enabled", true)) {
                        publishResult(topic, TestResult::skipped("test disabled"), payload);
                        return;
                    }
                    dispatch(topic, t, payload);
                    return;
                }
            }
            publishResult(topic, TestResult::fail("topic not found in tests.json"), payload);
        } catch (const std::exception& e) {
            publishResult(topic, TestResult::fail(std::string("lookup error: ") + e.what()),
                          payload);
        }
    });
}

void TestEngine::dispatch(const std::string& topic, const nlohmann::json& testCfg,
                          const std::string& requestPayload)
{
    std::fprintf(stderr, "[TestEngine] dispatch topic=%s module=%s async=%d\n",
                 topic.c_str(),
                 testCfg.value("module", std::string("<missing>")).c_str(),
                 testCfg.value("async", false) ? 1 : 0);

    std::unique_ptr<ITestModule> mod;
    try {
        mod = ModuleRegistry::instance().create(
            testCfg.at("module").get<std::string>());
    } catch (const std::exception& e) {
        publishResult(topic,
            TestResult::fail(std::string("bad config: ") + e.what()),
            requestPayload);
        return;
    }

    if (!mod) {
        publishResult(topic, TestResult::fail("unknown module"), requestPayload);
        return;
    }

    auto params = testCfg.value("params", nlohmann::json::object());
    if (!params.is_object()) {
        params = nlohmann::json::object();
    }
    params["topic"] = topic;
    params["request_payload"] = requestPayload;

    TestContext ctx(
        drivers_,
        config_,
        std::move(params),
        [this, topic, requestPayload](const TestResult& progress) {
            publishResult(topic, progress, requestPayload);
        });

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

    // 若当前线程已是 worker 线程，强制走 async，避免 enqueueAndWait 自死锁
    bool forceAsync = asyncQueue_.isWorkerThread();
    if (testCfg.value("async", false) || forceAsync) {
        asyncQueue_.enqueue([task = std::move(task), topic, requestPayload, this]() mutable {
            publishResult(topic, task(), requestPayload);
        });
    } else {
        publishResult(topic, asyncQueue_.enqueueAndWait(std::move(task)), requestPayload);
    }
}

void TestEngine::publishResult(const std::string& topic, const TestResult& result,
                               const std::string& requestPayload)
{
    std::lock_guard<std::mutex> lock(mqttMutex_);
    const bool isProgress = result.data.is_object() &&
                            result.data.value("progress", false);
    const char* statusStr = "UNKNOWN";
    switch (result.status) {
        case TestResult::Status::Pass:    statusStr = "PASS"; break;
        case TestResult::Status::Fail:    statusStr = "FAIL"; break;
        case TestResult::Status::Skipped: statusStr = "SKIP"; break;
    }
    if (isProgress) {
        fprintf(stdout, "[Progress] topic=%s detail=%s\n",
                topic.c_str(), result.detail.c_str());
    } else {
        fprintf(stdout, "[Result] topic=%s status=%s detail=%s\n",
                topic.c_str(), statusStr, result.detail.c_str());
    }
    fflush(stdout);

#ifdef HAVE_MOSQUITTO
    if (mqttClient_) {
        try {
            const auto errorCode = errorCodeFromResult(result);
            const auto answerTopic = answerTopicFor(topic);
            const auto payload = (topic == "37R")
                ? result.responseExtra
                : protocolResponsePayload(requestPayload, errorCode,
                                          result.responseExtra);
            const int rc = mosquitto_publish(mqttClient_, nullptr, answerTopic.c_str(),
                                             static_cast<int>(payload.size()),
                                             payload.data(), 2, false);
            if (rc != MOSQ_ERR_SUCCESS) {
                std::fprintf(stderr, "[TestEngine] publish %s failed: %s\n",
                             answerTopic.c_str(), mosquitto_strerror(rc));
            } else {
                std::fprintf(stderr, "[TestEngine] publish %s payload_len=%zu error_code=%u\n",
                             answerTopic.c_str(), payload.size(), errorCode);
            }
        } catch (...) {
            // Protocol serialization failure, ignore.
        }
    }
#endif
}

} // namespace ft
