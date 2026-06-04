#include "core/TestEngine.h"
#include "core/ModuleRegistry.h"
#include "core/TestResult.h"
#include "core/TestContext.h"
#include "config/PlatformConfig.h"
#include <cstdio>
#include <fstream>

namespace ft {

TestEngine::TestEngine(DriverRegistry& drivers, PlatformConfig& cfg)
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
        for (auto& t : j["tests"]) {
            auto name = t.at("module").get<std::string>();
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
    // TODO: lookup testCfg by topic, call dispatch
}

void TestEngine::dispatch(const std::string& topic, const nlohmann::json& testCfg) {
    // TODO: as defined in spec v1.3-final
}

void TestEngine::publishResult(const std::string& topic, const TestResult& result) {
    // TODO: mosquitto_publish with mutex
}

} // namespace ft
