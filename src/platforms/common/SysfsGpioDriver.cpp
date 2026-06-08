#include "platforms/common/SysfsGpioDriver.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <sys/stat.h>

namespace ft {

namespace {

bool gpioPathExists(int gpio)
{
    const auto path = "/sys/class/gpio/gpio" + std::to_string(gpio);
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

} // namespace

bool SysfsGpioDriver::exportGpio(int gpio) {
    if (gpioPathExists(gpio)) {
        return true;
    }

    std::ofstream f("/sys/class/gpio/export");
    f << gpio;
    if (!f.good() && !gpioPathExists(gpio)) {
        std::fprintf(stderr, "[SysfsGpio] export failed gpio=%d\n", gpio);
        return false;
    }

    for (int i = 0; i < 50; ++i) {
        if (gpioPathExists(gpio)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::fprintf(stderr, "[SysfsGpio] export timeout gpio=%d\n", gpio);
    return false;
}

bool SysfsGpioDriver::setDirection(int gpio, const std::string& dir) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction");
    f << dir;
    const bool ok = f.good();
    if (!ok) {
        std::fprintf(stderr, "[SysfsGpio] direction failed gpio=%d dir=%s\n",
                     gpio, dir.c_str());
    }
    return ok;
}

bool SysfsGpioDriver::setEdge(int gpio, const std::string& edge) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/edge");
    f << edge;
    const bool ok = f.good();
    if (!ok) {
        std::fprintf(stderr, "[SysfsGpio] edge failed gpio=%d edge=%s\n",
                     gpio, edge.c_str());
    }
    return ok;
}

bool SysfsGpioDriver::write(int gpio, int value) {
    std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value");
    f << value;
    const bool ok = f.good();
    if (!ok) {
        std::fprintf(stderr, "[SysfsGpio] write failed gpio=%d value=%d\n",
                     gpio, value);
    }
    return ok;
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
