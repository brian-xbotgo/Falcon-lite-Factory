#pragma once
#include "core/DriverRegistry.h"
#include "core/TestResult.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace ft {

class PlatformConfig;

class TestContext {
public:
    using ProgressCallback = std::function<void(const TestResult&)>;

    TestContext(const DriverRegistry& drivers,
                const PlatformConfig& cfg,
                nlohmann::json params,
                ProgressCallback progress = {})
        : drivers_(drivers),
          config_(cfg),
          params_(std::move(params)),
          progress_(std::move(progress)) {}

    template<class I>
    std::unique_ptr<I> create() const {
        return drivers_.create<I>();
    }

    const PlatformConfig& config() const { return config_; }
    const nlohmann::json& params() const { return params_; }
    void publishProgress(const TestResult& result) const {
        if (progress_) {
            progress_(result);
        }
    }

private:
    const DriverRegistry& drivers_;
    const PlatformConfig& config_;
    nlohmann::json params_;
    ProgressCallback progress_;
};

} // namespace ft
