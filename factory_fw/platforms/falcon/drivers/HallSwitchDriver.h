#pragma once
#include "platforms/common/interface/IHallDriver.h"
#include <cstdint>
#include <mutex>

namespace ft {

class HallSwitchDriver : public IHallDriver {
public:
    bool init() override;
    void deinit() override;
    float readValue() override;
    bool isInitialized() const override { return initialized_; }

private:
    static unsigned char crc8(const unsigned char data[]);

    int fd_ = -1;
    bool initialized_ = false;
    mutable std::mutex mutex_;
};

} // namespace ft
