#pragma once
#include <string>

namespace ft {

class IGpioDriver {
public:
    virtual ~IGpioDriver() = default;
    virtual bool exportGpio(int gpio) = 0;
    virtual bool setDirection(int gpio, const std::string& dir) = 0;
    virtual bool setEdge(int gpio, const std::string& edge) = 0;
    virtual bool write(int gpio, int value) = 0;
    virtual int  read(int gpio) = 0;
    virtual bool unexport(int gpio) = 0;
};

} // namespace ft
