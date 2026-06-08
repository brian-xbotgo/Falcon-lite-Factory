#pragma once

#include <string>

namespace ft {

class GpioController {
public:
    static bool exportGpio(int gpio);
    static bool setDirection(int gpio, const std::string& dir);
    static bool setEdge(int gpio, const std::string& edge);
    static bool write(int gpio, int value);
    static int read(int gpio);
    static bool unexport(int gpio);
};

} // namespace ft
