#include "drivers/HallSwitchDriver.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace ft {

namespace {

constexpr uint8_t kAds1110Config60Sps = 0x04;
constexpr int kAds1110MinCode60Sps = 8192;
constexpr float kAds1110Reference = 2.048f;
constexpr float kInvalidHallValue = 9999.0f;

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

HallSwitchDriver::HallSwitchDriver(nlohmann::json config)
    : config_(parseConfig(config))
{
}

int HallSwitchDriver::parseIntValue(const nlohmann::json& value, int fallback)
{
    try {
        if (value.is_number_integer()) {
            return value.get<int>();
        }
        if (value.is_string()) {
            return std::stoi(value.get<std::string>(), nullptr, 0);
        }
    } catch (...) {
    }
    return fallback;
}

float HallSwitchDriver::parseFloatValue(const nlohmann::json& value, float fallback)
{
    try {
        if (value.is_number()) {
            return value.get<float>();
        }
        if (value.is_string()) {
            return std::stof(value.get<std::string>());
        }
    } catch (...) {
    }
    return fallback;
}

HallSwitchDriver::Config HallSwitchDriver::parseConfig(const nlohmann::json& root)
{
    Config cfg;
    try {
        if (!root.contains("i2c") || !root.at("i2c").contains("hall")) {
            return cfg;
        }

        const auto& hall = root.at("i2c").at("hall");
        cfg.driver = lowerCopy(hall.value("driver", cfg.driver));
        if (hall.contains("bus")) {
            cfg.bus = parseIntValue(hall.at("bus"), cfg.bus);
        }
        if (hall.contains("bus_candidates") && hall.at("bus_candidates").is_array()) {
            cfg.busCandidates.clear();
            for (const auto& item : hall.at("bus_candidates")) {
                const int bus = parseIntValue(item, -1);
                if (bus >= 0 &&
                    std::find(cfg.busCandidates.begin(), cfg.busCandidates.end(), bus) ==
                        cfg.busCandidates.end()) {
                    cfg.busCandidates.push_back(bus);
                }
            }
        }
        if (hall.contains("addr")) {
            cfg.addr = parseIntValue(hall.at("addr"), cfg.addr);
        }
        if (hall.contains("sample_count")) {
            cfg.sampleCount = parseIntValue(hall.at("sample_count"), cfg.sampleCount);
        }
        if (hall.contains("sample_interval_ms")) {
            cfg.sampleIntervalMs = parseIntValue(hall.at("sample_interval_ms"),
                                                 cfg.sampleIntervalMs);
        }
        if (hall.contains("min_voltage")) {
            cfg.minVoltage = parseFloatValue(hall.at("min_voltage"), cfg.minVoltage);
        }
        if (hall.contains("max_voltage")) {
            cfg.maxVoltage = parseFloatValue(hall.at("max_voltage"), cfg.maxVoltage);
        }
    } catch (...) {
    }

    cfg.sampleCount = std::max(1, cfg.sampleCount);
    cfg.sampleIntervalMs = std::max(0, cfg.sampleIntervalMs);
    if (cfg.bus >= 0 &&
        std::find(cfg.busCandidates.begin(), cfg.busCandidates.end(), cfg.bus) ==
            cfg.busCandidates.end()) {
        cfg.busCandidates.insert(cfg.busCandidates.begin(), cfg.bus);
    }
    if (cfg.busCandidates.empty()) {
        cfg.busCandidates.push_back(cfg.bus);
    }
    return cfg;
}

std::string HallSwitchDriver::busPath(int bus)
{
    return "/dev/i2c-" + std::to_string(bus);
}

bool HallSwitchDriver::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    if (config_.driver != "ads1110") {
        std::fprintf(stderr,
                     "[HallSwitch] unsupported hall driver=%s bus=%d addr=0x%02x\n",
                     config_.driver.c_str(), config_.bus, config_.addr);
        return false;
    }

    return initAds1110();
}

bool HallSwitchDriver::initAds1110()
{
    std::vector<int> candidates = config_.busCandidates;
    if (config_.bus >= 0 &&
        std::find(candidates.begin(), candidates.end(), config_.bus) == candidates.end()) {
        candidates.insert(candidates.begin(), config_.bus);
    }

    for (int bus : candidates) {
        if (initAds1110OnBus(bus)) {
            return true;
        }
    }

    std::fprintf(stderr,
                 "[HallSwitch] ADS1110 init failed addr=0x%02x candidates=%zu\n",
                 config_.addr, candidates.size());
    return false;
}

bool HallSwitchDriver::initAds1110OnBus(int bus)
{
    const auto path = busPath(bus);
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        std::fprintf(stderr, "[HallSwitch] open %s failed: %s\n",
                     path.c_str(), std::strerror(errno));
        return false;
    }

    uint8_t config = kAds1110Config60Sps;
    struct i2c_msg msg {};
    msg.addr = static_cast<uint16_t>(config_.addr);
    msg.flags = 0;
    msg.len = 1;
    msg.buf = &config;

    struct i2c_rdwr_ioctl_data msgset {};
    msgset.msgs = &msg;
    msgset.nmsgs = 1;

    int retry = 0;
    for (; retry < 3; ++retry) {
        if (::ioctl(fd, I2C_RDWR, &msgset) >= 0) {
            break;
        }
        if (retry < 2) {
            ::usleep(100000);
        }
    }

    if (retry >= 3) {
        std::fprintf(stderr,
                     "[HallSwitch] ADS1110 config failed bus=%d addr=0x%02x config=0x%02x err=%s\n",
                     bus, config_.addr, config, std::strerror(errno));
        ::close(fd);
        return false;
    }

    ::usleep(200000);
    fd_ = fd;
    config_.bus = bus;
    initialized_ = true;
    std::fprintf(stderr,
                 "[HallSwitch] initialized driver=ADS1110 bus=%d addr=0x%02x config=0x%02x sample_count=%d interval_ms=%d min=%.3fV max=%.3fV\n",
                 config_.bus, config_.addr, config, config_.sampleCount,
                 config_.sampleIntervalMs, config_.minVoltage, config_.maxVoltage);
    return true;
}

void HallSwitchDriver::deinit()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    initialized_ = false;
}

float HallSwitchDriver::readValue()
{
    std::lock_guard<std::mutex> lock(mutex_);
    float value = kInvalidHallValue;
    if (!readAds1110(value)) {
        return kInvalidHallValue;
    }
    return value;
}

bool HallSwitchDriver::readAds1110(float& value)
{
    if (fd_ < 0) {
        std::fprintf(stderr, "[HallSwitch] read failed: fd invalid\n");
        return false;
    }

    uint8_t buffer[3] = {};
    struct i2c_msg msg {};
    msg.addr = static_cast<uint16_t>(config_.addr);
    msg.flags = I2C_M_RD;
    msg.len = sizeof(buffer);
    msg.buf = buffer;

    struct i2c_rdwr_ioctl_data msgset {};
    msgset.msgs = &msg;
    msgset.nmsgs = 1;

    int retry = 0;
    for (; retry < 3; ++retry) {
        if (::ioctl(fd_, I2C_RDWR, &msgset) >= 0) {
            break;
        }
        if (retry < 2) {
            ::usleep(10000);
        }
    }

    if (retry >= 3) {
        std::fprintf(stderr,
                     "[HallSwitch] ADS1110 read failed bus=%d addr=0x%02x err=%s\n",
                     config_.bus, config_.addr, std::strerror(errno));
        return false;
    }

    const int16_t code = static_cast<int16_t>(
        (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1]);
    if (code == 0) {
        std::fprintf(stderr,
                     "[HallSwitch] ADS1110 read invalid raw=0 status=0x%02x\n",
                     buffer[2]);
        return false;
    }

    value = (static_cast<float>(code) / kAds1110MinCode60Sps) * kAds1110Reference;
    std::fprintf(stderr,
                 "[HallSwitch] ADS1110 read raw=%d voltage=%.4fV status=0x%02x\n",
                 code, value, buffer[2]);
    return true;
}

} // namespace ft
