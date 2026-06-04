#pragma once
#include <string>

namespace ft {

struct BatteryInfo {
    double voltage_v = 0.0;
    int percent = 0;
    std::string model;
    bool valid = false;
};

class IBatteryDriver {
public:
    virtual ~IBatteryDriver() = default;
    virtual bool init() = 0;
    virtual BatteryInfo read() = 0;
    virtual std::string getSysfsPath() const = 0;
};

} // namespace ft
