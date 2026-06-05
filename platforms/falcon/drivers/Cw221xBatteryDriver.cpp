#include "drivers/Cw221xBatteryDriver.h"
#include "common/ShellUtils.h"
#include <cstdio>

namespace ft {

// TODO(RK3576): 核对电量计型号、sysfs 路径是否与 RK3576 一致
static constexpr const char* SYSFS_UEVENT = "/sys/class/power_supply/cw221X-bat/uevent";

std::string Cw221xBatteryDriver::ueventValue(const std::string& content,
                                              const std::string& key)
{
    size_t pos = 0;
    while (pos < content.size()) {
        auto eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.rfind(key + "=", 0) == 0) {
            return line.substr(key.size() + 1);
        }
    }
    return {};
}

bool Cw221xBatteryDriver::init()
{
    std::string content = read_file(SYSFS_UEVENT);
    return !content.empty();
}

BatteryInfo Cw221xBatteryDriver::read()
{
    BatteryInfo info = {};
    std::string content = read_file(SYSFS_UEVENT);
    if (content.empty()) {
        std::fprintf(stderr, "[Battery] cannot read %s\n", SYSFS_UEVENT);
        return info;
    }

    std::string voltage_str = ueventValue(content, "POWER_SUPPLY_VOLTAGE_NOW");
    if (!voltage_str.empty()) {
        long voltage_uv = std::atol(voltage_str.c_str());
        info.voltage_v = static_cast<double>(voltage_uv) / 1000000.0;
    }

    std::string capacity_str = ueventValue(content, "POWER_SUPPLY_CAPACITY");
    if (!capacity_str.empty()) {
        info.percent = std::atoi(capacity_str.c_str());
    }

    std::string model_str = ueventValue(content, "POWER_SUPPLY_MODEL_NAME");
    info.model = model_str.empty() ? "CW221X" : model_str;
    info.valid = true;
    return info;
}

} // namespace ft
