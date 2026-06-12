#pragma once
#include "platforms/common/interface/IHallDriver.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ft {

class HallSwitchDriver : public IHallDriver {
public:
    explicit HallSwitchDriver(nlohmann::json config = {});

    bool init() override;
    void deinit() override;
    float readValue() override;
    bool isInitialized() const override { return initialized_; }
    const char* valueUnit() const override;
    bool valueIsMillitesla() const override { return activeDriver_ == ActiveDriver::Kth3601; }

private:
    enum class ActiveDriver {
        None,
        Ads1110,
        Ads122u04,
        Kth3601,
    };

    struct Config {
        std::string driver = "falcon_hw";
        int bus = 3;
        std::vector<int> busCandidates = {3};
        int addr = 0x48;
        int kthAddr = 0x6a;
        std::vector<int> kthBusCandidates = {2};
        std::vector<std::string> uartCandidates = {"/dev/ttyS9"};
        std::string hwVersionAdc = "/sys/bus/iio/devices/iio:device0/in_voltage2_raw";
        int sampleCount = 120;
        int sampleIntervalMs = 50;
        float minVoltage = 1.6f;
        float maxVoltage = 2.0f;
        float minAbsMt = 15.0f;
    };

    bool initFalconHw();
    bool initAds1110();
    bool initAds1110OnBus(int bus);
    bool readAds1110(float& value);
    bool initAds122u04();
    bool initAds122u04OnUart(const std::string& path);
    bool readAds122u04(float& value);
    bool ads122WriteReg(uint8_t reg, uint8_t value);
    bool ads122ReadReg(uint8_t reg, uint8_t& value);
    bool ads122SendCmd(uint8_t cmd);
    int ads122SendRecv(const uint8_t* tx, size_t txLen,
                       uint8_t* rx, size_t rxLen, int timeoutMs);
    bool initKth3601();
    bool initKth3601OnBus(int bus);
    bool readKth3601(float& value);
    static uint8_t kth3601Crc(const uint8_t data[4]);
    static std::string busPath(int bus);
    static uint64_t monotonicMs();
    static int parseIntValue(const nlohmann::json& value, int fallback);
    static float parseFloatValue(const nlohmann::json& value, float fallback);
    static Config parseConfig(const nlohmann::json& root);
    int readFalconHwVersion() const;

    Config config_;
    int fd_ = -1;
    bool initialized_ = false;
    ActiveDriver activeDriver_ = ActiveDriver::None;
    std::string activePath_;
    bool ads122CrcEnabled_ = false;
    bool ads122DcntEnabled_ = false;
    uint8_t ads122LastDcnt_ = 0;
    mutable std::mutex mutex_;
};

} // namespace ft
