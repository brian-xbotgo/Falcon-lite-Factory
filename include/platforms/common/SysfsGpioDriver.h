#pragma once
#include "platforms/common/interface/IGpioDriver.h"

namespace ft {

class SysfsGpioDriver : public IGpioDriver {
public:
    bool exportGpio(int gpio) override;
    bool setDirection(int gpio, const std::string& dir) override;
    bool setEdge(int gpio, const std::string& edge) override;
    bool write(int gpio, int value) override;
    int  read(int gpio) override;
    bool unexport(int gpio) override;
};

} // namespace ft
