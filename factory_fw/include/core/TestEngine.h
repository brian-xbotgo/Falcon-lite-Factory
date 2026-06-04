#pragma once
#include "DriverRegistry.h"
#include "AsyncTaskQueue.h"
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

namespace ft {

class PlatformConfig;

class TestEngine {
public:
    TestEngine(DriverRegistry& drivers, PlatformConfig& cfg);
    ~TestEngine();

    bool loadTestConfig(const std::string& path);
    void run();
    void stop();

    void onMqttMessage(const std::string& topic, const std::string& payload);

private:
    void dispatch(const std::string& topic, const nlohmann::json& testCfg);
    void publishResult(const std::string& topic, const struct TestResult& result);

    DriverRegistry& drivers_;
    PlatformConfig& config_;
    AsyncTaskQueue asyncQueue_;
    nlohmann::json testConfigs_;
    std::mutex mqttMutex_;
    bool running_ = true;
};

} // namespace ft
