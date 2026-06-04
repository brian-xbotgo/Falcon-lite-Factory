#pragma once
#include <cstdint>

namespace ft {

class IDisplayDriver {
public:
    virtual ~IDisplayDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;
    virtual uint32_t taskHandler() = 0;
    virtual void setBatteryPercent(int pct) = 0;
    virtual void setBatteryModel(const char* model) = 0;
    virtual void setKeyValid(bool valid) = 0;
    virtual bool isInitialized() const = 0;
};

} // namespace ft
