#pragma once
#include "core/DriverRegistry.h"
#include "core/AsyncTaskQueue.h"
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>
#include <atomic>

namespace ft {

struct TestResult;
class PlatformConfig;

class TestEngine {
public:
    TestEngine(const DriverRegistry& drivers, const PlatformConfig& cfg);
    ~TestEngine();

    bool loadTestConfig(const std::string& path);
    void run();
    void stop();

    void onMqttMessage(const std::string& topic, const std::string& payload);

private:
    void dispatch(const std::string& topic, const nlohmann::json& testCfg);
    void publishResult(const std::string& topic, const TestResult& result);
    bool connectMqtt();

    const DriverRegistry& drivers_;
    const PlatformConfig& config_;
    AsyncTaskQueue asyncQueue_;
    nlohmann::json testConfigs_;
    std::mutex mqttMutex_;
    std::atomic<bool> running_{true};
    struct mosquitto* mqttClient_ = nullptr;
};

} // namespace ft
