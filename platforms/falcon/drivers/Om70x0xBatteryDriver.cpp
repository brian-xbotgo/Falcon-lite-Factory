#include "drivers/Om70x0xBatteryDriver.h"
#include "common/ShellUtils.h"
#include <cstdlib>
#include <cstdio>

namespace ft {

namespace {

constexpr const char* kSysfsUevent =
    "/sys/class/power_supply/om70X0X-bat/uevent";

} // namespace

std::string Om70x0xBatteryDriver::ueventValue(const std::string& content,
                                               const std::string& key)
{
    size_t pos = 0;
    while (pos < content.size()) {
        auto eol = content.find('\n', pos);
        if (eol == std::string::npos) {
            eol = content.size();
        }
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.rfind(key + "=", 0) == 0) {
            return line.substr(key.size() + 1);
        }
    }
    return {};
}

bool Om70x0xBatteryDriver::init()
{
    return !read_file(kSysfsUevent).empty();
}

BatteryInfo Om70x0xBatteryDriver::read()
{
    BatteryInfo info = {};
    std::string content = read_file(kSysfsUevent);
    if (content.empty()) {
        std::fprintf(stderr, "[Battery] cannot read %s\n", kSysfsUevent);
        return info;
    }

    std::string voltageStr = ueventValue(content, "POWER_SUPPLY_VOLTAGE_NOW");
    if (!voltageStr.empty()) {
        long voltageUv = std::atol(voltageStr.c_str());
        info.voltage_v = static_cast<double>(voltageUv) / 1000000.0;
    }

    std::string capacityStr = ueventValue(content, "POWER_SUPPLY_CAPACITY");
    if (!capacityStr.empty()) {
        info.percent = std::atoi(capacityStr.c_str());
    }

    std::string modelStr = ueventValue(content, "POWER_SUPPLY_MODEL_NAME");
    if (modelStr.empty()) {
        modelStr = ueventValue(content, "POWER_SUPPLY_NAME");
    }
    info.model = modelStr.empty() ? "om70X0X" : modelStr;
    info.valid = true;
    return info;
}

} // namespace ft
