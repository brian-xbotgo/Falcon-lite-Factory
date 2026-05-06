#pragma once

#include <string>
#include <cstdint>

namespace ft {

class I2cController {
public:
    static int readRegister(int bus, int addr, int reg);
    static bool writeRegister(int bus, int addr, int reg, uint8_t value);

private:
    static std::string executeI2cCommand(const std::string& cmd);
};

} // namespace ft
