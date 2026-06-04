#include "platforms/common/SysfsGpioDriver.h"
#include <fstream>
#include <string>

namespace ft {

bool SysfsGpioDriver::exportGpio(int gpio) {
    std::ofstream f("/sys/class/gpio/export");
    f << gpio;
    return f.good();
}

bool SysfsGpioDriver::setDirection(int gpio, const std::string& dir) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction");
    f << dir;
    return f.good();
}

bool SysfsGpioDriver::setEdge(int gpio, const std::string& edge) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/edge");
    f << edge;
    return f.good();
}

bool SysfsGpioDriver::write(int gpio, int value) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value");
    f << value;
    return f.good();
}

int SysfsGpioDriver::read(int gpio) {
    std::ifstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value");
    int v = -1;
    f >> v;
    return v;
}

bool SysfsGpioDriver::unexport(int gpio) {
    std::ofstream f("/sys/class/gpio/unexport");
    f << gpio;
    return f.good();
}

} // namespace ft
