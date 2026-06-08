#pragma once
#include "platforms/common/interface/IHallDriver.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>

namespace ft {

class HallSwitchDriver : public IHallDriver {
public:
    explicit HallSwitchDriver(nlohmann::json config = {});

    bool init() override;
    void deinit() override;
    float readValue() override;
    bool isInitialized() const override { return initialized_; }

private:
    struct Config {
        std::string driver = "ads1110";
        int bus = 3;
        int addr = 0x48;
        int sampleCount = 120;
        int sampleIntervalMs = 50;
        float minVoltage = 1.6f;
        float maxVoltage = 2.0f;
    };

    bool initAds1110();
    bool readAds1110(float& value);
    std::string busPath() const;
    static int parseIntValue(const nlohmann::json& value, int fallback);
    static float parseFloatValue(const nlohmann::json& value, float fallback);
    static Config parseConfig(const nlohmann::json& root);

    Config config_;
    int fd_ = -1;
    bool initialized_ = false;
    mutable std::mutex mutex_;
};

} // namespace ft
