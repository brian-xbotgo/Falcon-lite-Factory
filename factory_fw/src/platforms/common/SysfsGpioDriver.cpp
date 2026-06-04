#include "platforms/common/interface/IGpioDriver.h"
#include <fstream>
#include <string>

namespace ft {

class SysfsGpioDriver : public IGpioDriver {
public:
    bool exportGpio(int gpio) override {
        std::ofstream f("/sys/class/gpio/export");
        f << gpio;
        return f.good();
    }
    bool setDirection(int gpio, const std::string& dir) override {
        std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction");
        f << dir;
        return f.good();
    }
    bool setEdge(int gpio, const std::string& edge) override {
        std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/edge");
        f << edge;
        return f.good();
    }
    bool write(int gpio, int value) override {
        std::ofstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value");
        f << value;
        return f.good();
    }
    int read(int gpio) override {
        std::ifstream f("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value");
        int v = -1;
        f >> v;
        return v;
    }
    bool unexport(int gpio) override {
        std::ofstream f("/sys/class/gpio/unexport");
        f << gpio;
        return f.good();
    }
};

} // namespace ft
