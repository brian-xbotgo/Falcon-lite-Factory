#pragma once
#include "core/DriverRegistry.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace ft {

class PlatformConfig;

class TestContext {
public:
    TestContext(const DriverRegistry& drivers,
                const PlatformConfig& cfg,
                nlohmann::json params)
        : drivers_(drivers), config_(cfg), params_(std::move(params)) {}

    template<class I>
    std::unique_ptr<I> create() const {
        return drivers_.create<I>();
    }

    const PlatformConfig& config() const { return config_; }
    const nlohmann::json& params() const { return params_; }

private:
    const DriverRegistry& drivers_;
    const PlatformConfig& config_;
    nlohmann::json params_;
};

} // namespace ft
