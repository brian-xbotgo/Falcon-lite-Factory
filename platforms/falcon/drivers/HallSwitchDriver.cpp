#include "drivers/HallSwitchDriver.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/select.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace ft {

namespace {

constexpr uint8_t kAds1110Config60Sps = 0x04;
constexpr int kAds1110MinCode60Sps = 8192;
constexpr float kAds1110Reference = 2.048f;
constexpr float kInvalidHallValue = 9999.0f;

constexpr uint8_t kAdsSyncByte = 0x55;
constexpr uint8_t kAdsCmdStart = 0x08;
constexpr uint8_t kAdsCmdStop = 0x0a;
constexpr uint8_t kAdsCmdRdata = 0x10;
constexpr uint8_t kAdsCmdRreg = 0x20;
constexpr uint8_t kAdsCmdWreg = 0x40;
constexpr uint8_t kAdsCmdReset = 0x06;
constexpr uint8_t kAdsRegConfig0 = 0x00;
constexpr uint8_t kAdsRegConfig1 = 0x01;
constexpr uint8_t kAdsRegConfig2 = 0x02;
constexpr uint8_t kAdsRegConfig3 = 0x03;
constexpr uint8_t kAdsRegConfig4 = 0x04;
constexpr uint8_t kAdsConfig0 = 0xb1;
constexpr uint8_t kAdsConfig1 = 0x2c;
constexpr uint8_t kAdsConfig2 = 0x00;
constexpr uint8_t kAdsConfig3 = 0x00;
constexpr uint8_t kAdsConfig4 = 0x00;
constexpr float kAds122VrefVolts = 3.3f;

constexpr uint8_t kKthExitModeCmd = 0x80;
constexpr uint8_t kKthModeCmd = 0x18;
constexpr uint8_t kKthReadReg = 0x48;
constexpr int kKthRawToMillitesla = 90;

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
        if (hall.contains("kth_addr")) {
            cfg.kthAddr = parseIntValue(hall.at("kth_addr"), cfg.kthAddr);
        }
        if (hall.contains("kth_bus")) {
            const int kthBus = parseIntValue(hall.at("kth_bus"), -1);
            if (kthBus >= 0) {
                cfg.kthBusCandidates.insert(cfg.kthBusCandidates.begin(), kthBus);
            }
        }
        if (hall.contains("kth_bus_candidates") &&
            hall.at("kth_bus_candidates").is_array()) {
            cfg.kthBusCandidates.clear();
            for (const auto& item : hall.at("kth_bus_candidates")) {
                const int bus = parseIntValue(item, -1);
                if (bus >= 0 &&
                    std::find(cfg.kthBusCandidates.begin(),
                              cfg.kthBusCandidates.end(), bus) ==
                        cfg.kthBusCandidates.end()) {
                    cfg.kthBusCandidates.push_back(bus);
                }
            }
        }
        if (hall.contains("uart") && hall.at("uart").is_string()) {
            cfg.uartCandidates.clear();
            cfg.uartCandidates.push_back(hall.at("uart").get<std::string>());
        }
        if (hall.contains("uart_candidates") && hall.at("uart_candidates").is_array()) {
            cfg.uartCandidates.clear();
            for (const auto& item : hall.at("uart_candidates")) {
                if (!item.is_string()) {
                    continue;
                }
                const auto path = item.get<std::string>();
                if (!path.empty() &&
                    std::find(cfg.uartCandidates.begin(), cfg.uartCandidates.end(), path) ==
                        cfg.uartCandidates.end()) {
                    cfg.uartCandidates.push_back(path);
                }
            }
        }
        if (hall.contains("hw_version_adc") && hall.at("hw_version_adc").is_string()) {
            cfg.hwVersionAdc = hall.at("hw_version_adc").get<std::string>();
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
        if (hall.contains("min_abs_mt")) {
            cfg.minAbsMt = parseFloatValue(hall.at("min_abs_mt"), cfg.minAbsMt);
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
    if (cfg.kthBusCandidates.empty()) {
        cfg.kthBusCandidates.push_back(2);
    }
    if (cfg.uartCandidates.empty()) {
        cfg.uartCandidates.push_back("/dev/ttyS9");
    }
    return cfg;
}

std::string HallSwitchDriver::busPath(int bus)
{
    return "/dev/i2c-" + std::to_string(bus);
}

uint64_t HallSwitchDriver::monotonicMs()
{
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

int HallSwitchDriver::readFalconHwVersion() const
{
    FILE* fp = std::fopen(config_.hwVersionAdc.c_str(), "r");
    int raw = 0;
    if (!fp) {
        std::fprintf(stderr,
                     "[HallSwitch] hw version adc open failed path=%s err=%s, fallback HWv1.5\n",
                     config_.hwVersionAdc.c_str(), std::strerror(errno));
        return 0;
    }

    if (std::fscanf(fp, "%d", &raw) != 1) {
        std::fprintf(stderr,
                     "[HallSwitch] hw version adc read failed path=%s, fallback HWv1.5\n",
                     config_.hwVersionAdc.c_str());
        std::fclose(fp);
        return 0;
    }
    std::fclose(fp);

    int version = 0;
    if (raw >= 0 && raw <= 30) {
        version = 1;
    } else if (raw >= 1628 && raw <= 1688) {
        version = 2;
    } else if (raw >= 3649 && raw <= 3709) {
        version = 3;
    }

    std::fprintf(stderr,
                 "[HallSwitch] Falcon hw_version raw=%d mapped=%d\n",
                 raw, version);
    return version;
}

bool HallSwitchDriver::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    if (config_.driver == "falcon_hw" || config_.driver == "falcon") {
        return initFalconHw();
    }
    if (config_.driver == "ads1110") {
        return initAds1110();
    }
    if (config_.driver == "ads122u04") {
        return initAds122u04();
    }
    if (config_.driver == "kth3601") {
        return initKth3601();
    }
    if (config_.driver == "auto") {
        if (initAds1110()) {
            return true;
        }
        std::fprintf(stderr, "[HallSwitch] ADS1110 unavailable, trying KTH3601 I2C\n");
        if (initKth3601()) {
            return true;
        }
        std::fprintf(stderr, "[HallSwitch] KTH3601 unavailable, trying ADS122U04 UART\n");
        return initAds122u04();
    }

    std::fprintf(stderr,
                 "[HallSwitch] unsupported hall driver=%s bus=%d addr=0x%02x\n",
                 config_.driver.c_str(), config_.bus, config_.addr);
    return false;
}

bool HallSwitchDriver::initFalconHw()
{
    const int hwVersion = readFalconHwVersion();
    switch (hwVersion) {
    case 1:
    case 2:
    case 3:
        std::fprintf(stderr,
                     "[HallSwitch] Falcon HWv%d selected ADS1110 path=/dev/i2c-3 addr=0x48\n",
                     hwVersion);
        if (initAds1110OnBus(3)) {
            return true;
        }
        std::fprintf(stderr,
                     "[HallSwitch] Falcon HWv%d ADS1110 init failed path=/dev/i2c-3 addr=0x48\n",
                     hwVersion);
        return false;
    default:
        std::fprintf(stderr,
                     "[HallSwitch] Falcon HWv1.5/unknown selected ADS122U04 path=/dev/ttyS9\n");
        if (initAds122u04OnUart("/dev/ttyS9")) {
            return true;
        }
        std::fprintf(stderr,
                     "[HallSwitch] Falcon HWv1.5/unknown ADS122U04 init failed path=/dev/ttyS9\n");
        return false;
    }
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
    activeDriver_ = ActiveDriver::Ads1110;
    activePath_ = path;
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
        if (activeDriver_ == ActiveDriver::Ads122u04) {
            ads122SendCmd(kAdsCmdStop);
        }
        ::close(fd_);
        fd_ = -1;
    }
    initialized_ = false;
    activeDriver_ = ActiveDriver::None;
    activePath_.clear();
}

float HallSwitchDriver::readValue()
{
    std::lock_guard<std::mutex> lock(mutex_);
    float value = kInvalidHallValue;
    bool ok = false;
    if (activeDriver_ == ActiveDriver::Ads1110) {
        ok = readAds1110(value);
    } else if (activeDriver_ == ActiveDriver::Ads122u04) {
        ok = readAds122u04(value);
    } else if (activeDriver_ == ActiveDriver::Kth3601) {
        ok = readKth3601(value);
    }
    if (!ok) {
        return kInvalidHallValue;
    }
    return value;
}

const char* HallSwitchDriver::valueUnit() const
{
    return activeDriver_ == ActiveDriver::Kth3601 ? "mT" : "V";
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

uint8_t HallSwitchDriver::kth3601Crc(const uint8_t data[4])
{
    static constexpr uint8_t kCrcTable[256] = {
        0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
        0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
        0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
        0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
        0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
        0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
        0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
        0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
        0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
        0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
        0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
        0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
        0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
        0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
        0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
        0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
        0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
        0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
        0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
        0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
        0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
        0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
        0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
        0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
        0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
        0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
        0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
        0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
        0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
        0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
        0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
        0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3,
    };

    uint8_t crcData[8] = {};
    crcData[6] = data[1];
    crcData[7] = data[2];

    uint8_t crc = 0;
    for (uint8_t item : crcData) {
        crc = kCrcTable[crc ^ item];
    }
    return static_cast<uint8_t>(crc ^ 0x55);
}

bool HallSwitchDriver::initKth3601()
{
    std::vector<int> candidates = config_.kthBusCandidates;
    for (int bus : candidates) {
        if (initKth3601OnBus(bus)) {
            return true;
        }
    }

    std::fprintf(stderr,
                 "[HallSwitch] KTH3601 init failed addr=0x%02x candidates=%zu\n",
                 config_.kthAddr, candidates.size());
    return false;
}

bool HallSwitchDriver::initKth3601OnBus(int bus)
{
    const auto path = busPath(bus);
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        std::fprintf(stderr, "[HallSwitch] open %s failed: %s\n",
                     path.c_str(), std::strerror(errno));
        return false;
    }

    auto writeByte = [&](uint8_t value, int retryDelayUs) {
        struct i2c_msg msg {};
        msg.addr = static_cast<uint16_t>(config_.kthAddr);
        msg.flags = 0;
        msg.len = 1;
        msg.buf = &value;

        struct i2c_rdwr_ioctl_data msgset {};
        msgset.msgs = &msg;
        msgset.nmsgs = 1;

        for (int retry = 0; retry < 3; ++retry) {
            if (::ioctl(fd, I2C_RDWR, &msgset) >= 0) {
                return true;
            }
            if (retry < 2) {
                ::usleep(retryDelayUs);
            }
        }
        std::fprintf(stderr,
                     "[HallSwitch] KTH3601 write bus=%d addr=0x%02x cmd=0x%02x failed err=%s\n",
                     bus, config_.kthAddr, value, std::strerror(errno));
        return false;
    };

    if (!writeByte(kKthExitModeCmd, 100000)) {
        ::close(fd);
        return false;
    }
    ::usleep(500000);

    if (!writeByte(kKthModeCmd, 100000)) {
        ::close(fd);
        return false;
    }
    ::usleep(200000);

    fd_ = fd;
    config_.bus = bus;
    initialized_ = true;
    activeDriver_ = ActiveDriver::Kth3601;
    activePath_ = path;
    std::fprintf(stderr,
                 "[HallSwitch] initialized driver=KTH3601 bus=%d addr=0x%02x sample_count=%d interval_ms=%d min_abs=%.3fmT\n",
                 config_.bus, config_.kthAddr, config_.sampleCount,
                 config_.sampleIntervalMs, config_.minAbsMt);
    return true;
}

bool HallSwitchDriver::readKth3601(float& value)
{
    if (fd_ < 0) {
        std::fprintf(stderr, "[HallSwitch] KTH3601 read failed: fd invalid\n");
        return false;
    }

    uint8_t reg = kKthReadReg;
    uint8_t buffer[4] = {};
    struct i2c_msg msgs[2] {};
    msgs[0].addr = static_cast<uint16_t>(config_.kthAddr);
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;
    msgs[1].addr = static_cast<uint16_t>(config_.kthAddr);
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = sizeof(buffer);
    msgs[1].buf = buffer;

    struct i2c_rdwr_ioctl_data msgset {};
    msgset.msgs = msgs;
    msgset.nmsgs = 2;

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
                     "[HallSwitch] KTH3601 read failed bus=%d addr=0x%02x reg=0x%02x err=%s\n",
                     config_.bus, config_.kthAddr, reg, std::strerror(errno));
        return false;
    }

    const uint8_t crc = kth3601Crc(buffer);
    if (crc != buffer[3]) {
        std::fprintf(stderr,
                     "[HallSwitch] KTH3601 crc failed bytes=0x%02x 0x%02x 0x%02x 0x%02x calc=0x%02x\n",
                     buffer[0], buffer[1], buffer[2], buffer[3], crc);
        return false;
    }

    const int16_t raw = static_cast<int16_t>(
        (static_cast<uint16_t>(buffer[1]) << 8) | buffer[2]);
    value = static_cast<float>(raw) / static_cast<float>(kKthRawToMillitesla);
    std::fprintf(stderr,
                 "[HallSwitch] KTH3601 read raw=%d value=%.4fmT bytes=0x%02x 0x%02x 0x%02x 0x%02x\n",
                 raw, value, buffer[0], buffer[1], buffer[2], buffer[3]);
    return true;
}

bool HallSwitchDriver::initAds122u04()
{
    for (const auto& path : config_.uartCandidates) {
        if (initAds122u04OnUart(path)) {
            return true;
        }
    }

    std::fprintf(stderr,
                 "[HallSwitch] ADS122U04 init failed candidates=%zu\n",
                 config_.uartCandidates.size());
    return false;
}

bool HallSwitchDriver::initAds122u04OnUart(const std::string& path)
{
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        std::fprintf(stderr, "[HallSwitch] open %s failed: %s\n",
                     path.c_str(), std::strerror(errno));
        return false;
    }

    struct termios options {};
    if (tcgetattr(fd, &options) < 0) {
        std::fprintf(stderr, "[HallSwitch] tcgetattr %s failed: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return false;
    }

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    options.c_cc[VTIME] = 10;
    options.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        std::fprintf(stderr, "[HallSwitch] tcsetattr %s failed: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    fd_ = fd;
    activeDriver_ = ActiveDriver::Ads122u04;
    activePath_ = path;
    ads122CrcEnabled_ = false;
    ads122DcntEnabled_ = false;
    ads122LastDcnt_ = 0;

    ads122SendCmd(kAdsCmdReset);
    ::usleep(100000);

    const struct {
        uint8_t reg;
        uint8_t value;
    } regs[] = {
        {kAdsRegConfig0, kAdsConfig0},
        {kAdsRegConfig1, kAdsConfig1},
        {kAdsRegConfig2, kAdsConfig2},
        {kAdsRegConfig3, kAdsConfig3},
        {kAdsRegConfig4, kAdsConfig4},
    };

    for (const auto& item : regs) {
        if (!ads122WriteReg(item.reg, item.value)) {
            std::fprintf(stderr,
                         "[HallSwitch] ADS122U04 write reg=0x%02x value=0x%02x failed path=%s\n",
                         item.reg, item.value, path.c_str());
            ::close(fd_);
            fd_ = -1;
            activeDriver_ = ActiveDriver::None;
            activePath_.clear();
            return false;
        }

        uint8_t readback = 0;
        if (!ads122ReadReg(item.reg, readback) ||
            (item.reg == kAdsRegConfig4
                 ? ((readback & 0x7f) != (item.value & 0x7f))
                 : readback != item.value)) {
            std::fprintf(stderr,
                         "[HallSwitch] ADS122U04 verify reg=0x%02x wrote=0x%02x read=0x%02x failed path=%s\n",
                         item.reg, item.value, readback, path.c_str());
            ::close(fd_);
            fd_ = -1;
            activeDriver_ = ActiveDriver::None;
            activePath_.clear();
            return false;
        }
        std::fprintf(stderr,
                     "[HallSwitch] ADS122U04 reg=0x%02x verified value=0x%02x\n",
                     item.reg, readback);
    }

    ads122DcntEnabled_ = false;
    ads122CrcEnabled_ = false;
    ads122SendCmd(kAdsCmdStart);
    tcflush(fd_, TCIFLUSH);

    initialized_ = true;
    std::fprintf(stderr,
                 "[HallSwitch] initialized driver=ADS122U04 uart=%s sample_count=%d interval_ms=%d min=%.3fV max=%.3fV\n",
                 path.c_str(), config_.sampleCount, config_.sampleIntervalMs,
                 config_.minVoltage, config_.maxVoltage);
    return true;
}

bool HallSwitchDriver::ads122SendCmd(uint8_t cmd)
{
    if (fd_ < 0) {
        return false;
    }
    uint8_t tx[2] = {kAdsSyncByte, cmd};
    return ::write(fd_, tx, sizeof(tx)) == static_cast<ssize_t>(sizeof(tx));
}

int HallSwitchDriver::ads122SendRecv(const uint8_t* tx, size_t txLen,
                                     uint8_t* rx, size_t rxLen, int timeoutMs)
{
    if (fd_ < 0 || !rx) {
        return -1;
    }
    if (txLen > 0 && tx) {
        const ssize_t written = ::write(fd_, tx, txLen);
        if (written != static_cast<ssize_t>(txLen)) {
            return -1;
        }
    }

    size_t total = 0;
    const uint64_t deadline = monotonicMs() + static_cast<uint64_t>(timeoutMs);
    while (total < rxLen && monotonicMs() < deadline) {
        const uint64_t now = monotonicMs();
        const int remainMs = now >= deadline ? 0 : static_cast<int>(deadline - now);
        struct timeval timeout {};
        timeout.tv_sec = remainMs / 1000;
        timeout.tv_usec = (remainMs % 1000) * 1000;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);
        const int ret = select(fd_ + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            continue;
        }

        const ssize_t got = ::read(fd_, rx + total, rxLen - total);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (got == 0) {
            continue;
        }
        total += static_cast<size_t>(got);
    }
    return static_cast<int>(total);
}

bool HallSwitchDriver::ads122WriteReg(uint8_t reg, uint8_t value)
{
    if (fd_ < 0) {
        return false;
    }
    if (initialized_ && activeDriver_ == ActiveDriver::Ads122u04) {
        ads122SendCmd(kAdsCmdStop);
        tcflush(fd_, TCIFLUSH);
    }
    uint8_t tx[3] = {
        kAdsSyncByte,
        static_cast<uint8_t>(kAdsCmdWreg | ((reg << 1) & 0x0e)),
        value,
    };
    const ssize_t sent = ::write(fd_, tx, sizeof(tx));
    tcdrain(fd_);
    return sent == static_cast<ssize_t>(sizeof(tx));
}

bool HallSwitchDriver::ads122ReadReg(uint8_t reg, uint8_t& value)
{
    if (fd_ < 0) {
        return false;
    }
    tcflush(fd_, TCIFLUSH);
    uint8_t tx[2] = {
        kAdsSyncByte,
        static_cast<uint8_t>(kAdsCmdRreg | ((reg << 1) & 0x0e)),
    };
    uint8_t rx[1] = {0};
    const int got = ads122SendRecv(tx, sizeof(tx), rx, sizeof(rx), 100);
    if (got != static_cast<int>(sizeof(rx))) {
        return false;
    }
    value = rx[0];
    return true;
}

bool HallSwitchDriver::readAds122u04(float& value)
{
    if (fd_ < 0) {
        std::fprintf(stderr, "[HallSwitch] ADS122U04 read failed: fd invalid\n");
        return false;
    }

    uint8_t rx[8] = {};
    const size_t payloadLen = (ads122DcntEnabled_ ? 1 : 0) + 3;
    const size_t expect = payloadLen + (ads122CrcEnabled_ ? 2 : 0);
    int got = 0;
    const uint64_t deadline = monotonicMs() + 500ULL;
    while (monotonicMs() < deadline) {
        tcflush(fd_, TCIFLUSH);
        ads122SendCmd(kAdsCmdStart);
        uint8_t tx[2] = {kAdsSyncByte, kAdsCmdRdata};
        got = ads122SendRecv(tx, sizeof(tx), rx, expect, 400);
        if (got >= static_cast<int>(expect)) {
            break;
        }
        ::usleep(1000);
    }

    if (got < static_cast<int>(expect)) {
        std::fprintf(stderr,
                     "[HallSwitch] ADS122U04 read failed uart=%s got=%d expect=%zu\n",
                     activePath_.c_str(), got, expect);
        return false;
    }

    const size_t off = ads122DcntEnabled_ ? 1 : 0;
    int32_t raw = (static_cast<int32_t>(rx[off + 2]) << 16) |
                  (static_cast<int32_t>(rx[off + 1]) << 8) |
                  static_cast<int32_t>(rx[off + 0]);
    if (raw & 0x800000) {
        raw |= 0xff000000;
    }

    value = static_cast<float>(raw) * kAds122VrefVolts / 8388608.0f;
    std::fprintf(stderr,
                 "[HallSwitch] ADS122U04 read raw=%d voltage=%.4fV bytes=0x%02x 0x%02x 0x%02x\n",
                 raw, value, rx[off + 0], rx[off + 1], rx[off + 2]);
    return true;
}

} // namespace ft
